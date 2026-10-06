# Premium 購入・案内への動線

`Settings::PremiumPromotionAllowed()` を常に `false` にし、購入や案内を
表示する経路だけで参照する。設定項目や保存形式は追加していない。
`Main::Session::premium()`、`premiumPossible()`、ユーザーの契約フラグ、
サーバーの上限・権限判定は変更しない。

## 共通の停止箇所

- `settings/sections/settings_premium.{h,cpp}`: Premium 設定内容・検索登録・
  固定購読ボタン、`ShowPremium`、`StartPremiumPayment`、購読ボタン生成、
  共通プロモーション通知を停止する。
- `boxes/premium_preview_box.cpp`: 特典の案内・購入用プレビューを停止する。
  ステッカー自体のプレビューは購読ボタンを外して維持する。
- `window/window_session_controller.cpp`: Premium 設定への直接移動を拒否し、
  非 Premium アカウント向け速度制限プロモーション通知を抑制する。
- `core/local_url_handlers.cpp`: `premium_offer` と `premium_multigift` を
  処理済みとして終了し、購入画面やアカウント起動につなげない。
- `payments/payments_checkout_process.cpp`: 設定配信された Premium 請求書
  slug と、Premium ギフト・Premium 配布の型付き請求書を checkout 前に
  拒否する。呼び出し側の完了処理には `Cancelled` を返す。
  Stars 配布の `giveawayCredits` 付き請求書は維持する。

## 表示を閉じる箇所

以下のパスは `Telegram/SourceFiles/` からの相対パス。

| 画面・操作 | 対応したソース |
| --- | --- |
| 設定の Premium・Premium ギフト行、検索結果 | `settings/sections/settings_main.cpp`, `settings_premium.cpp` |
| Business の購読、無料アカウントの利用不可な特典一覧 | `settings/sections/settings_business.cpp` |
| ダイアログの Premium 提案・契約継続案内・対応するカスタム提案 | `dialogs/suggestions/suggestion_premium_offer.cpp`, `suggestion_premium_grace.cpp`, `suggestion_custom_promo.cpp` |
| 上限到達時の増量・購読・アカウント追加の案内 | `boxes/premium_limits_box.cpp`, `mainwidget.cpp` |
| 新規 Premium ギフト選択・購入、Premium 配布の選択 | `boxes/star_gift_box.cpp`, `info/channel_statistics/boosts/create_giveaway_box.cpp` |
| 無料アカウント向け Premium 必須 Stars ギフト、オークションの Premium 特典リンク | `info/peer_gifts/info_peer_gifts_common.cpp`, `boxes/star_gift_auction_box.cpp` |
| 絵文字・ステッカーの解除ボタンと案内通知 | `chat_helpers/emoji_list_widget.cpp`, `boxes/sticker_set_box.cpp`, `history/view/history_view_sticker_toast.cpp`, `data/stickers/data_stickers.cpp` |
| プロフィール・メインメニューの Premium バッジからの宣伝操作 | `info/profile/info_profile_top_bar.cpp`, `window/window_main_menu.cpp` |
| 読了時刻・最終アクセスの購読ボタン | `ui/boxes/show_or_premium_box.cpp` |
| プライバシーの購読・制限解除案内、参加者追加の案内 | `settings/settings_privacy_controllers.cpp`, `boxes/edit_privacy_box.cpp`, `boxes/peers/add_participants_box.cpp` |
| メッセージ入力欄・空チャット・ストーリー返信の Learn More / Unlock | `chat_helpers/message_field.cpp`, `history/view/history_view_about_view.cpp`, `history/view/controls/history_view_compose_controls.cpp`, `media/stories/media_stories_reply.cpp` |
| ロックされた音声文字起こし・要約ボタン | `history/view/history_view_transcribe_button.cpp` |
| 無料アカウントの保存済みタグ検索・フォルダーのタグ操作 | `dialogs/dialogs_search_tags.cpp`, `settings/sections/settings_folders.cpp`, `boxes/filters/edit_filter_box.cpp` |
| 類似チャンネルの解除、投稿検索の購読操作 | `info/similar_peers/info_similar_peers_widget.cpp`, `dialogs/ui/posts_search_intro.cpp` |
| 名前・プロフィール色の利用不可な適用ボタン、壁紙の相手側への適用案内 | `boxes/peers/edit_peer_color_box.cpp`, `boxes/background_preview_box.cpp` |
| リッチメッセージ購読、メッセージ効果の購読リンク | `iv/editor/iv_editor_session.cpp`, `menu/menu_send.cpp` |
| 無料アカウントの To-do 作成・転送制限・ステルスモードの案内操作 | `window/window_peer_menu.cpp`, `media/stories/media_stories_stealth.cpp` |
| 広告の Hide Ads / Premium の説明・早期閉じる操作、ストーリー保存のプロモーション | `menu/menu_sponsored.cpp`, `ui/chat/sponsored_message_bar.cpp`, `history/view/history_view_message.cpp`, `media/view/media_view_playback_sponsored.cpp`, `media_view_overlay_widget.cpp` |

## 維持するものと範囲

- 実際の Premium アカウントの既存機能、契約バッジ、上限値。
- Stars・TON 残高、一般のギフト送信、Stars 配布、ギフトの再販・管理。
  一般の「ギフト」は Stars の選択画面へ進む。Premium ギフト欄は出さない。
- 受け取った Premium ギフト、コードの表示・適用、履歴。旧 Premium ギフト
  の詳細は購読画面の代わりに期間を示す情報画面を開く。
- 購入済みの配布の実行。新たな Premium 配布の購入とは区別する。
- 通常のプライバシー公開操作、広告の通報・説明、サーバーの拒否理由。
  Premium 契約が必要な操作を有効にする処理は追加しない。
- 誕生日編集の `settings_information.cpp` とギフトコレクション管理の
  `info_peer_gifts_widget.cpp` にあるギフトアイコンは購読誘導ではない。
  `history_inner_widget.cpp` の一般ギフト操作も Stars 送信として維持する。
- メッセージ本文、外部サイト、任意のボットや一般請求書の内容はフィルター
  しない。設定配信された Premium slug と明示的な内部 Premium URL 以外の
  一般リンクから、外部サービスが何を販売するかまでは判定しない。

## 検証

- `git diff --check` は通過した。
- ソースの 15 条件を確認した。共通の画面・支払い・通知停止、設定直接移動、
  内部 URL、型付き請求書、旧ギフト情報、タグ・入力制限、Stars の経路、
  コレクティブルの詳細表示を含む。
- 変更されたテキストの LF・UTF-8 BOM なしを確認した。
  方針関数を使う 46 個の `.cpp` は対応するヘッダーを直接 include している。
- `main/main_session.{h,cpp}` と `data/data_user.{h,cpp}` に差分がないことを
  確認した。契約状態の偽装や `premiumPossible()` の一律変更はしていない。
- 独立したレビューで見つかった、無料アカウントの保存済みタグの無反応な
  操作と広告通報ボタンの波紋位置を修正した。

AGENTS.md の「Avoid building the project」に従い、アプリ全体をビルド・起動
していない。UI の実描画、Qt 型のコンパイル、実アカウント・サーバー通信の
動作は未検証。このソース監査を実行時の確認として扱わない。
