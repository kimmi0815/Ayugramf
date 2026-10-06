/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_translate_tracker.h"

#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "iv/iv_rich_page.h"
#include "lang/translate_provider.h"
#include "main/main_session.h"
#include "spellcheck/platform/platform_language.h"

namespace HistoryView {
namespace {

constexpr auto kEnoughForRecognition = 10;
constexpr auto kEnoughForTranslation = 6;
constexpr auto kMaxCheckInBunch = 100;
constexpr auto kRequestLengthLimit = 24 * 1024;
constexpr auto kRequestCountLimit = 20;

void ClearAutomaticTranslationSources(not_null<History*> history) {
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			if (const auto translation = view->data()->Get<
					HistoryMessageTranslation>()) {
				translation->automaticFrom = {};
			}
		}
	}
}

} // namespace

TranslateTracker::TranslateTracker(not_null<History*> history)
: _history(history)
, _provider(Ui::CreateTranslateProvider(&_history->session()))
, _api(&_history->session().mtp())
, _limit(kEnoughForRecognition) {
	setup();
}

TranslateTracker::~TranslateTracker() {
	cancelToRequest();
	cancelSentRequest();
}

rpl::producer<bool> TranslateTracker::trackingLanguage() const {
	return _trackingLanguage.value();
}

void TranslateTracker::setup() {
	const auto peer = _history->peer;
	peer->updateFull();
	_manualTranslatedTo = _history->translatedTo();

	using namespace rpl::mappers;
	_trackingLanguage = Core::App().settings().translateChatEnabledValue();
	_trackingLanguage.value() | rpl::on_next([=](bool tracking) {
		_trackingLifetime.destroy();
		if (tracking) {
			recognizeCollected();
			trackSkipLanguages();
		} else {
			checkRecognized({});
			stopAndRevert();
		}
	}, _lifetime);
	trackTranslationDisabled();
	_history->session().changes().historyUpdates(
		_history,
		Data::HistoryUpdate::Flag::TranslatedTo
	) | rpl::on_next([=] {
		translationTargetChanged();
	}, _lifetime);
	AyuSettings::getInstance().autoTranslationChanges(
	) | rpl::on_next([=](const AutoTranslationSettings &) {
		resetAutomaticTranslation();
	}, _lifetime);

	AyuSettings::getInstance().translationProviderChanges(
	) | rpl::on_next([=](TranslationProvider) {
		resetProvider();
	}, _lifetime);
}

bool TranslateTracker::enoughForRecognition() const {
	return _itemsForRecognize.size() >= kEnoughForRecognition;
}

void TranslateTracker::startBunch() {
	_addedInBunch = 0;
	translationTargetChanged();
	++_generation;
}

bool TranslateTracker::add(not_null<Element*> view) {
	const auto item = view->data();
	const auto only = view->isOnlyEmojiAndSpaces();
	if (only != OnlyEmojiAndSpaces::Unknown) {
		item->cacheOnlyEmojiAndSpaces(only == OnlyEmojiAndSpaces::Yes);
	}
	return add(item, false);
}

bool TranslateTracker::add(not_null<HistoryItem*> item) {
	return add(item, false);
}

bool TranslateTracker::add(
		not_null<HistoryItem*> item,
		bool skipDependencies,
		bool visible) {
	Expects(_addedInBunch >= 0);

	if ((item->out() && !item->history()->peer->autoTranslation())
		|| item->isService()
		|| !item->isRegular()
		|| item->isOnlyEmojiAndSpaces()) {
		return false;
	}
	if (!skipDependencies) {
		if (const auto reply = item->Get<HistoryMessageReply>()) {
			if (const auto to = reply->resolvedMessage.get()) {
				add(to, true, visible);
			}
		}
#if 0 // I hope this is not needed, although I'm not sure.
		if (item->groupId()) {
			if (const auto group = _history->owner().groups().find(item)) {
				for (const auto &other : group->items) {
					if (other != item) {
						add(other, true);
					}
				}
			}
		}
#endif
	}
	const auto id = item->fullId();
	const auto &text = item->originalText().text;
	const auto textHash = qHash(text);
	auto i = _itemsForRecognize.find(id);
	if (i != end(_itemsForRecognize)) {
		i->second.generation = _generation;
		if (i->second.textHash != textHash) {
			i->second.textHash = textHash;
			i->second.id = text;
			_itemsToRequest.erase(id);
			item->removeTranslationBit();
		}
	} else {
		i = _itemsForRecognize.emplace(id, ItemForRecognize{
			.generation = _generation,
			.textHash = textHash,
			.id = text,
		}).first;
		++_addedInBunch;
	}
	if (visible) {
		i->second.painted = true;
	}
	if (_trackingLanguage.current()
		|| AyuSettings::getInstance().autoTranslateEnabled()) {
		if (const auto unrecognized = std::get_if<QString>(&i->second.id)) {
			i->second.id = Platform::Language::Recognize(*unrecognized);
		}
	}
	const auto to = translationTarget(item);
	if (item->translationShowRequiresCheck(to)) {
		_switchTranslations[item] = to;
	}
	return true;
}

LanguageId TranslateTracker::translationTarget(
		not_null<HistoryItem*> item) const {
	if (const auto manual = _history->translatedTo()) {
		return manual;
	}
	const auto &settings = AyuSettings::getInstance();
	using Flag = PeerData::TranslationFlag;
	if (!settings.autoTranslateEnabled()
		|| _automaticTranslationSuppressed
		|| _manualTranslatedTo
		|| item->out()
		|| _history->peer->translationFlag() == Flag::Disabled
		|| item->history()->peer->translationFlag() == Flag::Disabled) {
		return {};
	}
	const auto entry = _itemsForRecognize.find(item->fullId());
	if (entry == end(_itemsForRecognize)
		|| !entry->second.painted
		|| !_history->owner().queryItemVisibility(item)) {
		return {};
	}
	const auto from = std::get_if<LanguageId>(&entry->second.id);
	const auto to = LanguageId::FromName(settings.autoTranslateTo());
	if (!from || !*from || *from == to) {
		return {};
	}
	return ranges::contains(settings.autoTranslateFrom(), from->twoLetterCode())
		? to
		: LanguageId();
}

LanguageId TranslateTracker::automaticSource(
		not_null<HistoryItem*> item) const {
	if (_history->translatedTo()) {
		return {};
	}
	const auto entry = _itemsForRecognize.find(item->fullId());
	if (entry != end(_itemsForRecognize)) {
		if (const auto id = std::get_if<LanguageId>(&entry->second.id)) {
			return *id;
		}
	}
	return {};
}

void TranslateTracker::refreshTranslations() {
	cancelToRequest();
	cancelSentRequest();
	_switchTranslations.clear();
	const auto owner = &_history->owner();
	for (const auto &id : base::take(_automaticTranslations)) {
		if (const auto item = owner->message(id)) {
			item->translationShowRequiresRequest({});
		}
	}
	for (const auto &[id, entry] : _itemsForRecognize) {
		if (const auto item = owner->message(id)) {
			const auto to = translationTarget(item);
			if (item->translationShowRequiresCheck(to)) {
				switchTranslation(item, to);
			}
		}
	}
	requestSome();
}

void TranslateTracker::resetAutomaticTranslation() {
	_automaticTranslationSuppressed = false;
	if (AyuSettings::getInstance().autoTranslateEnabled()) {
		recognizeCollected();
	}
	refreshTranslations();
}

void TranslateTracker::translationTargetChanged() {
	const auto to = _history->translatedTo();
	if (_manualTranslatedTo == to) {
		return;
	}
	if (_manualTranslatedTo && !to) {
		_automaticTranslationSuppressed = true;
	}
	_manualTranslatedTo = to;
	if (to) {
		ClearAutomaticTranslationSources(_history);
		if (const auto migrated = _history->migrateFrom()) {
			ClearAutomaticTranslationSources(migrated);
		}
	}
	refreshTranslations();
}

bool TranslateTracker::requestContentUnchanged(
		not_null<HistoryItem*> item) const {
	const auto content = _requestContents.find(item->fullId());
	return content != end(_requestContents)
		&& content->second.text == item->originalText()
		&& content->second.richPage == item->richPage();
}

void TranslateTracker::showTranslationIfDesired(
		not_null<HistoryItem*> item,
		LanguageId to) {
	if (translationTarget(item) == to) {
		item->translationShowRequiresRequest(to, automaticSource(item));
	} else {
		item->translationShowRequiresRequest({});
	}
}

void TranslateTracker::switchTranslation(
		not_null<HistoryItem*> item,
		LanguageId id) {
	_history->session().api().transcribes().checkSummaryToTranslate(
		item->fullId());
	if (item->translationShowRequiresRequest(id, automaticSource(item))) {
		_itemsToRequest.emplace(item->fullId(), ItemToRequest{
			.length = int(item->originalText().text.size()),
			.to = id,
			.rich = (_provider->supportsMessageId()
				&& (item->richPage() != nullptr)),
		});
	}
	if (id && !_history->translatedTo()) {
		_automaticTranslations.emplace(item->fullId());
	}
}

void TranslateTracker::finishBunch() {
	if (_addedInBunch > 0) {
		accumulate_max(_limit, _addedInBunch + kEnoughForRecognition);
		_addedInBunch = -1;
		applyLimit();
		if (_trackingLanguage.current()) {
			checkRecognized();
		}
	}
	if (!_switchTranslations.empty()) {
		auto switching = base::take(_switchTranslations);
		for (const auto &[item, id] : switching) {
			switchTranslation(item, id);
		}
		_switchTranslations = std::move(switching);
		_switchTranslations.clear();
	}
	requestSome();
}

void TranslateTracker::addBunchFromBlocks() {
	if (enoughForRecognition()) {
		return;
	}
	startBunch();
	const auto guard = gsl::finally([&] {
		finishBunch();
	});

	auto check = kMaxCheckInBunch;
	for (const auto &block : _history->blocks) {
		for (const auto &view : block->messages) {
			if (!check--
				|| (add(view->data(), false, false) && enoughForRecognition())) {
				return;
			}
		}
	}
}

void TranslateTracker::addBunchFrom(
		const std::vector<not_null<Element*>> &views) {
	if (enoughForRecognition()) {
		return;
	}
	startBunch();
	const auto guard = gsl::finally([&] {
		finishBunch();
	});

	auto check = kMaxCheckInBunch;
	for (const auto &view : views) {
		if (!check--
			|| (add(view->data(), false, false) && enoughForRecognition())) {
			return;
		}
	}
}

void TranslateTracker::cancelToRequest() {
	if (!_itemsToRequest.empty()) {
		const auto owner = &_history->owner();
		for (const auto &[id, entry] : base::take(_itemsToRequest)) {
			if (const auto item = owner->message(id)) {
				item->translationShowRequiresRequest({});
			}
		}
	}
}

void TranslateTracker::cancelSentRequest() {
	_api.request(base::take(_richRequestId)).cancel();
	if (_requestInProcess) {
		const auto owner = &_history->owner();
		for (const auto &id : base::take(_requested)) {
			if (const auto item = owner->message(id)) {
				item->translationShowRequiresRequest({});
			}
		}
		++_requestToken;
		_requestInProcess = false;
	}
	_requestContents.clear();
}

void TranslateTracker::stopAndRevert() {
	cancelToRequest();
	cancelSentRequest();
	const auto owner = &_history->owner();
	for (const auto &[id, entry] : _itemsForRecognize) {
		if (const auto item = owner->message(id)) {
			if (item->translation()
				&& item->translationShowRequiresCheck({})) {
				item->translationShowRequiresRequest({});
			}
		}
	}
	_manualTranslatedTo = {};
	_history->translateTo({});
	if (const auto migrated = _history->migrateFrom()) {
		migrated->translateTo({});
	}
	refreshTranslations();
}

void TranslateTracker::resetProvider() {
	cancelToRequest();
	cancelSentRequest();
	_provider = Ui::CreateTranslateProvider(&_history->session());
	invalidateTranslations();
	refreshTranslations();
}

void TranslateTracker::invalidateTranslations() {
	const auto clear = [&](not_null<History*> history) {
		for (const auto &block : history->blocks) {
			for (const auto &view : block->messages) {
				const auto item = view->data();
				if (!item->Has<HistoryMessageTranslation>()) {
					continue;
				}
				item->removeTranslationBit();
				history->owner().requestItemTextRefresh(item);
			}
		}
	};
	clear(_history);
	if (const auto migrated = _history->migrateFrom()) {
		clear(migrated);
	}
	for (const auto &[id, entry] : _itemsForRecognize) {
		if (const auto item = _history->owner().message(id)) {
			if (item->Has<HistoryMessageTranslation>()) {
				item->removeTranslationBit();
				item->history()->owner().requestItemTextRefresh(item);
			}
		}
	}
}

void TranslateTracker::requestSome() {
	if (_requestInProcess || _itemsToRequest.empty()) {
		return;
	}
	const auto to = _itemsToRequest.back().second.to;
	_requested.clear();
	_requestContents.clear();
	_requested.reserve(_itemsToRequest.size());
	const auto session = &_history->session();
	const auto peerId = _itemsToRequest.back().first.peer;
	const auto rich = _itemsToRequest.back().second.rich;
	auto length = 0;
	for (auto i = _itemsToRequest.end(); i != _itemsToRequest.begin();) {
		--i;
		if (i->first.peer != peerId
			|| i->second.rich != rich
			|| i->second.to != to) {
			break;
		}
		length += i->second.length;
		_requested.push_back(i->first);
		i = _itemsToRequest.erase(i);
		if (_requested.size() >= kRequestCountLimit
			|| length >= kRequestLengthLimit) {
			break;
		}
	}
	if (_requested.empty()) {
		return;
	}
	if (rich) {
		requestSomeRich(to, peerId);
		return;
	}
	const auto owner = &session->data();
	auto requests = std::vector<Ui::TranslateProviderRequest>();
	requests.reserve(_requested.size());
	auto ids = std::vector<FullMsgId>();
	ids.reserve(_requested.size());
	for (const auto &id : _requested) {
		if (const auto item = owner->message(id)) {
			if (translationTarget(item) != to) {
				item->translationShowRequiresRequest({});
				continue;
			}
			_requestContents.emplace(id, RequestedContent{
				.text = item->originalText(),
				.richPage = item->richPage(),
			});
			requests.push_back(Ui::PrepareTranslateProviderRequest(
				_provider.get(),
				session->data().peer(id.peer),
				id.msg,
				item->originalText()));
			ids.push_back(id);
		}
	}
	_requested = std::move(ids);
	if (_requested.empty()) {
		requestSome();
		return;
	}
	_requestInProcess = true;
	const auto requestToken = ++_requestToken;
	const auto weak = base::make_weak(this);
	_provider->requestBatch(
		std::move(requests),
		to,
		[=](int index, Ui::TranslateProviderResult result) {
			if (!weak
				|| !_requestInProcess
				|| (_requestToken != requestToken)) {
				return;
			}
			if (index < 0 || index >= _requested.size()) {
				return;
			}
			const auto &id = _requested[index];
			if (const auto item = owner->message(id)) {
				if (!requestContentUnchanged(item)) {
					item->translationShowRequiresRequest({});
					owner->requestItemTextRefresh(item);
					return;
				}
				item->translationDone(
					to,
					result.text.value_or(TextWithEntities()));
				showTranslationIfDesired(item, to);
			}
		},
		[=] {
			if (!weak
				|| !_requestInProcess
				|| (_requestToken != requestToken)) {
				return;
			}
			_requestInProcess = false;
			_requested.clear();
			_requestContents.clear();
			requestSome();
		});
}

void TranslateTracker::requestSomeRich(LanguageId to, PeerId peerId) {
	const auto session = &_history->session();
	const auto owner = &session->data();
	const auto peer = owner->peerLoaded(peerId);
	if (!peer) {
		for (const auto &id : base::take(_requested)) {
			if (const auto item = owner->message(id)) {
				item->translationDone(to, TextWithEntities());
			}
		}
		requestSome();
		return;
	}
	auto mtpIds = QVector<MTPint>();
	mtpIds.reserve(_requested.size());
	auto ids = std::vector<FullMsgId>();
	ids.reserve(_requested.size());
	for (const auto &id : _requested) {
		const auto item = owner->message(id);
		if (item
			&& item->richPage()
			&& translationTarget(item) == to) {
			mtpIds.push_back(MTP_int(id.msg));
			ids.push_back(id);
			_requestContents.emplace(id, RequestedContent{
				.text = item->originalText(),
				.richPage = item->richPage(),
			});
		} else if (item) {
			item->translationShowRequiresRequest({});
		}
	}
	_requested = std::move(ids);
	if (_requested.empty()) {
		requestSome();
		return;
	}
	_requestInProcess = true;
	const auto requestToken = ++_requestToken;
	const auto finish = [=] {
		_richRequestId = 0;
		_requestInProcess = false;
		_requested.clear();
		_requestContents.clear();
		requestSome();
	};
	using Flag = MTPmessages_TranslateRichMessage::Flag;
	_richRequestId = _api.request(MTPmessages_TranslateRichMessage(
		MTP_flags(Flag::f_peer | Flag::f_id),
		peer->input(),
		MTP_vector<MTPint>(mtpIds),
		MTPVector<MTPInputRichMessage>(),
		MTP_string(to.twoLetterCode()),
		MTPstring()
	)).done([=](const MTPmessages_TranslatedRichMessage &result) {
		if (!_requestInProcess || (_requestToken != requestToken)) {
			return;
		}
		const auto &list = result.data().vresult().v;
		for (auto i = 0, count = int(_requested.size()); i != count; ++i) {
			if (const auto item = owner->message(_requested[i])) {
				if (!requestContentUnchanged(item)) {
					item->translationShowRequiresRequest({});
					owner->requestItemTextRefresh(item);
					continue;
				}
				item->translationDone(to, (i < list.size())
					? Iv::ParseRichPage(session, list[i])
					: nullptr);
				showTranslationIfDesired(item, to);
			}
		}
		finish();
	}).fail([=](const MTP::Error &) {
		if (!_requestInProcess || (_requestToken != requestToken)) {
			return;
		}
		for (const auto &id : _requested) {
			if (const auto item = owner->message(id)) {
				if (requestContentUnchanged(item)) {
					item->translationDone(to, TextWithEntities());
				} else {
					item->translationShowRequiresRequest({});
					owner->requestItemTextRefresh(item);
				}
			}
		}
		finish();
	}).send();
}

void TranslateTracker::applyLimit() {
	const auto generationProjection = [](const auto &pair) {
		return pair.second.generation;
	};
	const auto owner = &_history->owner();

	// Erase starting with oldest generation till items count is not too big.
	while (_itemsForRecognize.size() > _limit) {
		const auto oldest = ranges::min_element(
			_itemsForRecognize,
			ranges::less(),
			generationProjection
		)->second.generation;
		for (auto i = begin(_itemsForRecognize)
			; i != end(_itemsForRecognize);) {
			if (i->second.generation == oldest) {
				if (const auto j = _itemsToRequest.find(i->first)
					; j != end(_itemsToRequest)) {
					if (const auto item = owner->message(i->first)) {
						item->translationShowRequiresRequest({});
					}
					_itemsToRequest.erase(j);
				}
				i = _itemsForRecognize.erase(i);
			} else {
				++i;
			}
		}
	}
}

void TranslateTracker::recognizeCollected() {
	for (auto &[id, entry] : _itemsForRecognize) {
		if (const auto text = std::get_if<QString>(&entry.id)) {
			entry.id = Platform::Language::Recognize(*text);
		}
	}
}

void TranslateTracker::trackSkipLanguages() {
	Core::App().settings().skipTranslationLanguagesValue(
	) | rpl::on_next([=](const std::vector<LanguageId> &skip) {
		const auto wasOfferedFrom = _history->translateOfferedFrom();
		const auto wasTranslatedTo = _history->translatedTo();
		checkRecognized(skip);
		if (wasTranslatedTo
			&& wasOfferedFrom
			&& !_history->translateOfferedFrom()) {
			stopAndRevert();
		}
	}, _trackingLifetime);
}

void TranslateTracker::trackTranslationDisabled() {
	using PeerFlag = Data::PeerUpdate::Flag;
	_history->session().changes().peerFlagsValue(
		_history->peer,
		PeerFlag::TranslationDisabled
	) | rpl::skip(1) | rpl::on_next([=] {
		using TranslationFlag = PeerData::TranslationFlag;
		if (_history->peer->translationFlag() == TranslationFlag::Disabled) {
			stopAndRevert();
		} else {
			refreshTranslations();
		}
	}, _lifetime);
}

void TranslateTracker::checkRecognized() {
	checkRecognized(Core::App().settings().skipTranslationLanguages());
}

void TranslateTracker::checkRecognized(const std::vector<LanguageId> &skip) {
	if (!_trackingLanguage.current()) {
		_history->translateOfferFrom({});
		return;
	}
	auto languages = base::flat_map<LanguageId, int>();
	for (const auto &[id, entry] : _itemsForRecognize) {
		if (const auto id = std::get_if<LanguageId>(&entry.id)) {
			if (*id && !ranges::contains(skip, *id)) {
				++languages[*id];
			}
		}
	}
	using namespace base;
	const auto count = int(_itemsForRecognize.size());
	constexpr auto p = &flat_multi_map_pair_type<LanguageId, int>::second;
	const auto threshold = (count > kEnoughForRecognition)
		? (count * kEnoughForTranslation / kEnoughForRecognition)
		: _allLoaded
		? std::min(count, kEnoughForTranslation)
		: kEnoughForTranslation;
	const auto translatable = ranges::accumulate(
		languages,
		0,
		ranges::plus(),
		p);
	if (count < kEnoughForTranslation) {
		// Don't change offer by small amount of messages.
	} else if (translatable >= threshold) {
		_history->translateOfferFrom(
			ranges::max_element(languages, ranges::less(), p)->first);
	} else {
		_history->translateOfferFrom({});
	}
}

} // namespace HistoryView
