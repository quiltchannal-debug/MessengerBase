// OrangeM Desktop - контракт интерфейса главного окна (вся отрисовка внутри ui.cpp).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "api.h"

namespace om {
namespace ui {

// Разделы приложения
enum Section { SEC_CHATS = 0, SEC_FEED = 1, SEC_COMMUNITIES = 2, SEC_STORIES = 3, SEC_PROFILE = 4 };

// Состояние, которое заполняет сетевой поток и передаёт в UI через setState()
struct UiState {
  int section = SEC_CHATS;
  long long activeChat = 0;
  std::string search;                 // строка поиска по разделу
  std::string serverUrl;
  std::vector<ChatItem> chats;
  std::vector<Msg> msgs;
  std::vector<Post> posts;
  std::vector<Community> communities;
  std::vector<StoryGroup> stories;
  User me;
  std::string status;                 // строка состояния внизу окна
  std::string chattingWith;           // заголовок открытого чата
  bool peerOnline = false;
  bool loading = false;
  bool realtime = false;
};

// Действия, которые UI делегирует наружу (в main.cpp)
struct UiCallbacks {
  std::function<void(int section)> onSection;
  std::function<void(long long chatId)> onChatSelected;
  std::function<void(const std::string& text)> onSendMessage;
  std::function<void(const std::string& text)> onCreatePost;
  std::function<void(const std::string& query)> onSearch;
  std::function<void()> onSearchCleared;
  std::function<void(long long communityId, bool join)> onToggleCommunity;
  std::function<void(long long communityId)> onOpenCommunityChat;
  std::function<void(long long postId, bool like)> onLikePost;
  std::function<void(long long postId, const std::string& comment)> onCommentPost;
  std::function<void()> onAttach;               // открыть диалог выбора файла и отправить
  std::function<void()> onNewChat;              // создать личный чат по идентификатору
  std::function<void()> onRefresh;
  std::function<void()> onLogout;
  std::function<void(long long storyId)> onViewStory;
  std::function<void()> onCreateStory;
  std::function<void(const std::string& serverUrl)> onServerChanged;
};

// --- жизненный цикл ---
void setCallbacks(UiCallbacks cb);
void setState(const UiState& s);      // копирует состояние (вызывать в UI-потоке)
UiState& state();
void invalidate();                     // InvalidateRect всех окон UI
HWND createMainWindow(HINSTANCE hInst, const std::wstring& title);
void destroyMainWindow();
HWND mainWindow();
bool handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result); // true = обработано

// Сообщения приложения, которые UI передаёт наружу (в main.cpp):
// WM_APP+1 = данные обновлены, WM_APP+2 = realtime-событие (lParam = Json*),
// WM_APP+3 = ошибка (lParam = std::string*), WM_APP+4 = вошли в аккаунт, WM_APP+5 = выход.
void setAppMessageHandler(std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)> fn);

// --- вспомогательное ---
std::wstring messageInputText();
void setMessageInputText(const std::wstring& text);
void focusMessageInput();
void scrollChatToBottom();
void showToast(const std::string& text);          // всплывающая плашка снизу справа (2.5 c)
void setStatus(const std::string& text);          // нижняя строка состояния
void openSection(int section);

} // namespace ui
} // namespace om
