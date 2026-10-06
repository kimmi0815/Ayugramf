/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "base/weak_ptr.h"
#include "mtproto/sender.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/text/text_entity.h"

class History;
class HistoryItem;
namespace Iv {
struct RichPage;
} // namespace Iv
namespace Ui {
class TranslateProvider;
} // namespace Ui

namespace HistoryView {

class Element;

class TranslateTracker final : public base::has_weak_ptr {
public:
	explicit TranslateTracker(not_null<History*> history);
	~TranslateTracker();

	[[nodiscard]] bool enoughForRecognition() const;
	void startBunch();
	bool add(not_null<Element*> view);
	bool add(not_null<HistoryItem*> item);
	void finishBunch();

	void addBunchFromBlocks();
	void addBunchFrom(const std::vector<not_null<Element*>> &views);

	[[nodiscard]] rpl::producer<bool> trackingLanguage() const;

private:
	using MaybeLanguageId = std::variant<QString, LanguageId>;
	struct ItemForRecognize {
		uint64 generation = 0;
		size_t textHash = 0;
		MaybeLanguageId id;
		bool painted = false;
	};
	struct ItemToRequest {
		int length = 0;
		LanguageId to;
		bool rich = false;
	};
	struct RequestedContent {
		TextWithEntities text;
		std::shared_ptr<const Iv::RichPage> richPage;
	};

	void setup();
	bool add(
		not_null<HistoryItem*> item,
		bool skipDependencies,
		bool visible = true);
	[[nodiscard]] LanguageId translationTarget(
		not_null<HistoryItem*> item) const;
	[[nodiscard]] LanguageId automaticSource(
		not_null<HistoryItem*> item) const;
	void refreshTranslations();
	void resetAutomaticTranslation();
	void translationTargetChanged();
	void showTranslationIfDesired(
		not_null<HistoryItem*> item,
		LanguageId to);
	[[nodiscard]] bool requestContentUnchanged(
		not_null<HistoryItem*> item) const;
	void recognizeCollected();
	void trackSkipLanguages();
	void trackTranslationDisabled();
	void checkRecognized();
	void checkRecognized(const std::vector<LanguageId> &skip);
	void applyLimit();
	void requestSome();
	void requestSomeRich(LanguageId to, PeerId peerId);
	void cancelToRequest();
	void cancelSentRequest();
	void stopAndRevert();
	void switchTranslation(not_null<HistoryItem*> item, LanguageId id);
	void resetProvider();
	void invalidateTranslations();

	const not_null<History*> _history;
	std::unique_ptr<Ui::TranslateProvider> _provider;
	MTP::Sender _api;
	rpl::variable<bool> _trackingLanguage = false;
	base::flat_map<FullMsgId, ItemForRecognize> _itemsForRecognize;
	uint64 _generation = 0;
	LanguageId _manualTranslatedTo;
	int _limit = 0;
	int _addedInBunch = -1;
	bool _allLoaded = false;
	bool _automaticTranslationSuppressed = false;

	base::flat_map<not_null<HistoryItem*>, LanguageId> _switchTranslations;
	base::flat_set<FullMsgId> _automaticTranslations;
	base::flat_map<FullMsgId, ItemToRequest> _itemsToRequest;
	base::flat_map<FullMsgId, RequestedContent> _requestContents;
	std::vector<FullMsgId> _requested;
	mtpRequestId _richRequestId = 0;
	uint64 _requestToken = 0;
	bool _requestInProcess = false;

	rpl::lifetime _trackingLifetime;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView
