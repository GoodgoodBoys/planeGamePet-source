#pragma once

namespace plane_pet_about {
inline constexpr wchar_t kDeveloper[] = L"Ding";
// Dedicated feedback/privacy contact, also reflected in the packaged documents.
// An empty value is displayed as a placeholder, never as an actionable mail link.
inline constexpr wchar_t kFeedbackEmail[] = L"planepet_public@163.com";
inline constexpr wchar_t kMissingEmail[] = L"暂未设置";
inline constexpr wchar_t kDescription[] =
    L"与朋友配对的飞机桌宠。\n发送一个表情，或邀请朋友开一局。";
inline constexpr int kPrivacyResource = 191;
inline constexpr int kLicensesResource = 192;
}  // namespace plane_pet_about
