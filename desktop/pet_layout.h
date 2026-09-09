#pragma once
#include <windows.h>

namespace plane_pet_ui {

// Physical-pixel content inside the fixed 280 x 150 pet. Rendering, hit tests
// and regression fixtures share these rectangles; no state resizes the HWND.
struct InviteCardLayout {
  RECT panel;
  RECT message;
  RECT primary;
  RECT secondary;
  RECT countdown;
};

inline constexpr InviteCardLayout kIncomingInviteLayout{
    {42, 68, 238, 140}, {52, 75, 228, 99},
    {52, 102, 134, 130}, {146, 102, 228, 130}, {0, 0, 0, 0}};
inline constexpr InviteCardLayout kOutgoingInviteLayout{
    {18, 76, 262, 132}, {28, 86, 154, 122},
    {204, 86, 252, 122}, {0, 0, 0, 0}, {160, 86, 196, 122}};
inline constexpr RECT kInviteFeedbackPanel{28, 74, 252, 110};
inline constexpr RECT kInviteFeedbackMessage{38, 80, 242, 104};
// Local three-second notice is NOT a Waiting phase. Keep all emotes reachable.
inline constexpr InviteCardLayout kDndNoticeLayout{
    {18, 66, 262, 110}, {28, 76, 172, 100},
    {204, 76, 252, 100}, {0, 0, 0, 0}, {178, 76, 198, 100}};
// Self-status notices have no countdown/button: center in the actual card,
// never in an independently hard-coded rectangle from an older layout.
inline constexpr RECT kDndStatusTextRect{
    kDndNoticeLayout.panel.left + 10, kDndNoticeLayout.panel.top + 8,
    kDndNoticeLayout.panel.right - 10, kDndNoticeLayout.panel.bottom - 8};
inline constexpr int kDndStatusFontEmHeight = 16;
inline constexpr UINT kDndStatusTextFormat = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

constexpr bool HasInset(const RECT &outer, const RECT &inner, int minimum) {
  return inner.left >= outer.left + minimum && inner.top >= outer.top + minimum &&
         inner.right <= outer.right - minimum && inner.bottom <= outer.bottom - minimum;
}
static_assert(HasInset(kIncomingInviteLayout.panel, kIncomingInviteLayout.primary, 10));
static_assert(HasInset(kIncomingInviteLayout.panel, kIncomingInviteLayout.secondary, 10));
static_assert(HasInset(kOutgoingInviteLayout.panel, kOutgoingInviteLayout.primary, 10));
static_assert(kIncomingInviteLayout.secondary.left - kIncomingInviteLayout.primary.right == 12);
static_assert(kIncomingInviteLayout.primary.top - kIncomingInviteLayout.message.bottom == 3);
static_assert(kOutgoingInviteLayout.countdown.left - kOutgoingInviteLayout.message.right >= 6);
static_assert(kOutgoingInviteLayout.primary.left - kOutgoingInviteLayout.countdown.right >= 8);
static_assert(kInviteFeedbackPanel.bottom + 6 <= 116);  // idle emoji row starts at 116
static_assert(HasInset(kDndNoticeLayout.panel, kDndNoticeLayout.primary, 10));
static_assert(kDndNoticeLayout.panel.bottom + 6 <= 116);
static_assert(HasInset(kDndNoticeLayout.panel, kDndStatusTextRect, 8));
static_assert(kDndStatusTextRect.top + kDndStatusTextRect.bottom ==
              kDndNoticeLayout.panel.top + kDndNoticeLayout.panel.bottom);

}  // namespace plane_pet_ui
