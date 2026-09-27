// OrangeM Desktop — точка входа: окно входа, рабочая нить сети, realtime, самотест.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <functional>
#include <cstdio>

#include "api.h"
#include "ui.h"
#include "theme.h"
#include "util.h"

#pragma comment(lib, "comctl32.lib")

using namespace om;

#define WM_APP_UPDATE   (WM_APP + 1)
#define WM_APP_REALTIME (WM_APP + 2)
#define WM_APP_ERROR    (WM_APP + 3)
#define WM_APP_LOGGEDIN (WM_APP + 4)
#define WM_APP_LOGOUT   (WM_APP + 5)

static HINSTANCE g_inst = nullptr;
static HWND g_login = nullptr;
static HWND g_main = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static bool g_selftest = false;

// ------------------------------------------------------------------ общее состояние
struct Shared {
  std::mutex m;
  ui::UiState st;
};
static Shared g_shared;

static void postUpdate() { if (g_main) PostMessageW(g_main, WM_APP_UPDATE, 0, 0); }
static void postError(const std::string& text) {
  if (g_main) PostMessageW(g_main, WM_APP_ERROR, 0, (LPARAM)new std::string(text));
}

// ------------------------------------------------------------------ рабочая нить
enum CmdType {
  CMD_REFRESH, CMD_SECTION, CMD_OPEN_CHAT, CMD_SEND, CMD_CREATE_POST, CMD_SEARCH,
  CMD_TOGGLE_COMMUNITY, CMD_LIKE, CMD_COMMENT, CMD_ATTACH, CMD_NEW_CHAT, CMD_LOGOUT,
  CMD_VIEW_STORY, CMD_CREATE_STORY
};
struct Cmd {
  CmdType type;
  long long id = 0;
  bool flag = false;
  std::string text;
};

class Worker {
public:
  void start() { thread_ = std::thread([this] { run(); }); }
  void stop() {
    { std::lock_guard<std::mutex> lk(m_); quit_ = true; cv_.notify_all(); }
    if (thread_.joinable()) thread_.join();
  }
  void push(Cmd c) {
    { std::lock_guard<std::mutex> lk(m_); q_.push_back(std::move(c)); }
    cv_.notify_all();
  }

private:
  void run();
  void refreshAll();
  void loadSection(int section);

  std::mutex m_;
  std::condition_variable cv_;
  std::deque<Cmd> q_;
  bool quit_ = false;
  std::thread thread_;
};
static Worker g_worker;

static void setStatus(const std::string& s) {
  std::lock_guard<std::mutex> lk(g_shared.m);
  g_shared.st.status = s;
  postUpdate();
}

void Worker::refreshAll() {
  std::string err;
  std::vector<ChatItem> chats = api().chats(err);
  std::vector<StoryGroup> stories = api().stories(err);
  std::lock_guard<std::mutex> lk(g_shared.m);
  if (!chats.empty() || err.empty()) g_shared.st.chats = chats;
  g_shared.st.stories = stories;
  g_shared.st.realtime = api().realtimeConnected();
  postUpdate();
}

void Worker::loadSection(int section) {
  std::string err;
  {
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.section = section;
    g_shared.st.loading = true;
    g_shared.st.msgs.clear();
  }
  postUpdate();
  if (section == ui::SEC_CHATS) {
    auto chats = api().chats(err, "");
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.chats = chats;
    g_shared.st.loading = false;
    if (!err.empty()) g_shared.st.status = err;
  } else if (section == ui::SEC_FEED) {
    auto posts = api().feed("global", err);
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.posts = posts;
    g_shared.st.loading = false;
    if (!err.empty()) g_shared.st.status = err;
  } else if (section == ui::SEC_COMMUNITIES) {
    auto comms = api().communities("all", err);
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.communities = comms;
    g_shared.st.loading = false;
    if (!err.empty()) g_shared.st.status = err;
  } else if (section == ui::SEC_STORIES) {
    auto st = api().stories(err);
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.stories = st;
    g_shared.st.loading = false;
    if (!err.empty()) g_shared.st.status = err;
  } else if (section == ui::SEC_PROFILE) {
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.me = api().me;
    g_shared.st.loading = false;
  }
  g_shared.st.realtime = api().realtimeConnected();
  postUpdate();
}

void Worker::run() {
  for (;;) {
    Cmd c;
    {
      std::unique_lock<std::mutex> lk(m_);
      cv_.wait(lk, [this] { return quit_ || !q_.empty(); });
      if (quit_) return;
      c = q_.front();
      q_.pop_front();
    }
    std::string err;
    switch (c.type) {
      case CMD_REFRESH:
        refreshAll();
        break;
      case CMD_SECTION:
        loadSection((int)c.id);
        break;
      case CMD_OPEN_CHAT: {
        auto msgs = api().messages(c.id, err);
        if (!err.empty()) postError(err);
        std::lock_guard<std::mutex> lk(g_shared.m);
        g_shared.st.msgs = msgs;
        g_shared.st.activeChat = c.id;
        g_shared.st.chattingWith = c.text;
        if (!msgs.empty()) api().markRead(c.id, msgs.back().id);
        postUpdate();
        break;
      }
      case CMD_SEND: {
        Msg out;
        if (!api().sendMessage(c.id, c.text, out, err)) postError(err);
        auto msgs = api().messages(c.id, err);
        std::lock_guard<std::mutex> lk(g_shared.m);
        if (!msgs.empty()) g_shared.st.msgs = msgs;
        postUpdate();
        break;
      }
      case CMD_CREATE_POST: {
        if (!api().createPost("", c.text, err)) postError(err);
        else setStatus("Публикация отправлена");
        loadSection(ui::SEC_FEED);
        break;
      }
      case CMD_SEARCH: {
        std::string q = c.text;
        int section = 0;
        {
          std::lock_guard<std::mutex> lk(g_shared.m);
          section = g_shared.st.section;
          g_shared.st.search = q;
        }
        if (section == ui::SEC_FEED) {
          auto posts = api().feed(q.empty() ? "global" : "global", err, q);
          std::lock_guard<std::mutex> lk(g_shared.m);
          g_shared.st.posts = posts;
        } else if (section == ui::SEC_COMMUNITIES) {
          auto comms = api().communities("all", err, q);
          std::lock_guard<std::mutex> lk(g_shared.m);
          g_shared.st.communities = comms;
        } else {
          auto chats = api().chats(err, q);
          std::lock_guard<std::mutex> lk(g_shared.m);
          g_shared.st.chats = chats;
        }
        postUpdate();
        break;
      }
      case CMD_TOGGLE_COMMUNITY: {
        if (!api().joinCommunity(c.id, c.flag, err)) postError(err);
        loadSection(ui::SEC_COMMUNITIES);
        break;
      }
      case CMD_LIKE: {
        if (!api().likePost(c.id, c.flag, err)) postError(err);
        else {
          std::lock_guard<std::mutex> lk(g_shared.m);
          for (auto& p : g_shared.st.posts)
            if (p.id == c.id) { p.liked = c.flag; p.likes += c.flag ? 1 : -1; if (p.likes < 0) p.likes = 0; }
          postUpdate();
        }
        break;
      }
      case CMD_COMMENT: {
        if (!api().commentPost(c.id, c.text, err)) postError(err);
        else setStatus("Комментарий добавлен");
        break;
      }
      case CMD_ATTACH: {
        std::string url;
        if (!api().uploadFile(c.text, "image", url, err)) {
          postError(err);
          break;
        }
        Json b = Json::obj();
        b.set("body", "");
        b.set("attachment", url);
        b.set("attachment_kind", "image");
        HttpResponse r = api().http.post("/api/chats/" + std::to_string(c.id) + "/messages", b);
        if (!r.ok()) postError(r.errorText());
        auto msgs = api().messages(c.id, err);
        std::lock_guard<std::mutex> lk(g_shared.m);
        if (!msgs.empty()) g_shared.st.msgs = msgs;
        postUpdate();
        break;
      }
      case CMD_NEW_CHAT: {
        long long chatId = 0;
        if (!api().openDm(c.text, chatId, err)) postError(err);
        else {
          refreshAll();
          Cmd open;
          open.type = CMD_OPEN_CHAT;
          open.id = chatId;
          open.text = c.text;
          push(open);
        }
        break;
      }
      case CMD_LOGOUT:
        api().logout();
        if (g_main) PostMessageW(g_main, WM_APP_LOGOUT, 0, 0);
        break;
      case CMD_VIEW_STORY:
        api().viewStory(c.id);
        break;
      case CMD_CREATE_STORY:
        if (!api().createStory(c.text, "#f97316", err)) postError(err);
        else setStatus("История опубликована");
        loadSection(ui::SEC_STORIES);
        break;
    }
  }
}

// ------------------------------------------------------------------ реакция на realtime
static void handleRealtime(Json* jp) {
  std::unique_ptr<Json> j(jp);
  if (!j) return;
  std::string type = (*j)["type"].str();
  if (type == "message.new" || type == "message.edited" || type == "message.deleted") {
    long long chatId = (*j)["chat_id"].num();
    long long active = 0;
    {
      std::lock_guard<std::mutex> lk(g_shared.m);
      active = g_shared.st.activeChat;
    }
    Cmd c;
    c.type = CMD_OPEN_CHAT;
    c.id = chatId;
    if (chatId == active) g_worker.push(c);
    else g_worker.push({CMD_REFRESH, 0, false, ""});
  } else if (type == "notification") {
    std::string text = (*j)["notification"]["text"].str();
    if (!text.empty()) ui::showToast(text);
  } else if (type == "presence") {
    long long uid = (*j)["user_id"].num();
    bool online = (*j)["online"].boolean();
    std::lock_guard<std::mutex> lk(g_shared.m);
    for (auto& c : g_shared.st.chats)
      if (c.peerId == uid) c.peerOnline = online;
    postUpdate();
  } else if (type == "broadcast") {
    ui::showToast("OrangeM: " + (*j)["text"].str());
  } else if (type == "session_file.update") {
    std::string content = (*j)["content"].str();
    if (!content.empty()) {
      std::string path = appDataDir() + "\\flash-key.omkey";
      writeFileBytes(path, content);
      ui::showToast("Флеш-ключ обновлён в реальном времени");
    }
  }
}

// ------------------------------------------------------------------ обработка сообщений приложения
static bool appMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  switch (msg) {
    case WM_APP_UPDATE: {
      std::lock_guard<std::mutex> lk(g_shared.m);
      ui::setState(g_shared.st);
      ui::invalidate();
      result = 0;
      return true;
    }
    case WM_APP_REALTIME:
      handleRealtime((Json*)lp);
      result = 0;
      return true;
    case WM_APP_ERROR: {
      std::unique_ptr<std::string> s((std::string*)lp);
      if (s) {
        ui::showToast(*s);
        ui::setStatus(*s);
      }
      result = 0;
      return true;
    }
    case WM_APP_LOGOUT:
      ui::destroyMainWindow();
      DestroyWindow(g_main);
      g_main = nullptr;
      api().clearSession();
      if (g_login) {
        ShowWindow(g_login, SW_SHOW);
        SetForegroundWindow(g_login);
      }
      result = 0;
      return true;
    default:
      return false;
  }
}

// ------------------------------------------------------------------ окно входа
#define ID_SERVER 1001
#define ID_IDENT  1002
#define ID_PASS   1003
#define ID_LOGIN  1004
#define ID_FLASH  1005
#define ID_REG    1006
#define ID_STATUS 1007
#define ID_TOTP   1008

static HFONT g_fontUi = nullptr, g_fontBold = nullptr, g_fontSmall = nullptr;

static void makeFonts() {
  g_fontUi = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Segoe UI");
  g_fontBold = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
  g_fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
}

static std::wstring getText(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring s((size_t)n, L'\0');
  GetWindowTextW(h, &s[0], n + 1);
  return s;
}

static void setLoginStatus(const std::wstring& text, COLORREF color) {
  HWND st = GetDlgItem(g_login, ID_STATUS);
  if (!st) return;
  SetWindowTextW(st, text.c_str());
  (void)color;
  InvalidateRect(st, nullptr, TRUE);
}

static void enterApp() {
  std::string err;
  if (!api().loadMe(err)) {
    setLoginStatus(u2w(err), theme::danger);
    return;
  }
  {
    std::lock_guard<std::mutex> lk(g_shared.m);
    g_shared.st.me = api().me;
    g_shared.st.serverUrl = api().serverUrl();
    g_shared.st.section = ui::SEC_CHATS;
  }
  if (g_login) ShowWindow(g_login, SW_HIDE);
  if (!g_main) {
    g_main = ui::createMainWindow(g_inst, L"OrangeM");
    ShowWindow(g_main, SW_SHOWMAXIMIZED);
  } else {
    ShowWindow(g_main, SW_SHOW);
  }
  UpdateWindow(g_main);
  std::string wsErr;
  if (!api().startRealtime(wsErr)) ui::showToast("Realtime недоступен: " + wsErr);
  g_worker.push({CMD_SECTION, ui::SEC_CHATS, false, ""});
  g_worker.push({CMD_REFRESH, 0, false, ""});
}

static void doLogin() {
  std::wstring ident = getText(GetDlgItem(g_login, ID_IDENT));
  std::wstring pass = getText(GetDlgItem(g_login, ID_PASS));
  std::string err;
  setLoginStatus(L"Подключение…", theme::muted);
  if (api().loginPassword(w2u(ident), w2u(pass), err)) {
    if (api().needTotp) {
      setLoginStatus(L"Введите 6-значный код из приложения-аутентификатора в поле пароля и нажмите «Войти»",
                     theme::warn);
      SetWindowTextW(GetDlgItem(g_login, ID_PASS), L"");
      // повторное нажатие «Войти» с кодом трактуем как TOTP
      return;
    }
    enterApp();
    return;
  }
  if (api().needTotp) {
    if (api().loginTotp(w2u(pass), err)) {
      enterApp();
      return;
    }
  }
  setLoginStatus(u2w(err), theme::danger);
}

static void doFlashKey() {
  wchar_t file[MAX_PATH] = {0};
  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_login;
  ofn.lpstrFilter = L"Ключ OrangeM (*.omkey)\0*.omkey\0Все файлы\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&ofn)) return;
  std::string content;
  if (!readFileBytes(w2u(file), content)) {
    setLoginStatus(L"Не удалось прочитать файл ключа", theme::danger);
    return;
  }
  std::string rotated, err;
  setLoginStatus(L"Проверка ключа…", theme::muted);
  if (!api().loginSessionFile(content, rotated, err)) {
    setLoginStatus(u2w(err), theme::danger);
    return;
  }
  if (!rotated.empty()) writeFileBytes(w2u(file), rotated);
  enterApp();
}

static LRESULT CALLBACK loginProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wp) == ID_LOGIN) doLogin();
      else if (LOWORD(wp) == ID_FLASH) doFlashKey();
      else if (LOWORD(wp) == ID_REG) {
        // никаких служебных окон: подсказка выводится прямо в окне входа
        setLoginStatus(L"Регистрация — в веб-клиенте. После неё войдите здесь по почте, "
                       L"телефону или флеш-ключу.", theme::text);
      }
      return 0;
    case WM_DRAWITEM: {
      LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
      bool primary = d->CtlID == ID_LOGIN;
      HBRUSH bg = CreateSolidBrush(primary ? theme::orange : theme::panel2);
      FillRect(d->hDC, &d->rcItem, bg);
      DeleteObject(bg);
      if (!primary) {
        HBRUSH frame = CreateSolidBrush(theme::line2);
        FrameRect(d->hDC, &d->rcItem, frame);
        DeleteObject(frame);
      }
      wchar_t text[128] = {0};
      GetWindowTextW(d->hwndItem, text, 127);
      SetBkMode(d->hDC, TRANSPARENT);
      SetTextColor(d->hDC, primary ? theme::onOrange : theme::text);
      SelectObject(d->hDC, g_fontBold);
      RECT rc = d->rcItem;
      DrawTextW(d->hDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)wp;
      SetTextColor(dc, msg == WM_CTLCOLOREDIT ? theme::text : theme::muted);
      SetBkColor(dc, theme::bg2);
      static HBRUSH br = CreateSolidBrush(theme::bg2);
      return (LRESULT)br;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND createLoginWindow(HINSTANCE inst) {
  const wchar_t* cls = L"OrangeMLogin";
  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = loginProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(theme::bg);
  wc.lpszClassName = cls;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(101));
  wc.hIconSm = wc.hIcon;
  RegisterClassExW(&wc);

  int w = 460, h = 430;
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  HWND hwnd = CreateWindowExW(0, cls, L"OrangeM — вход", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                              x, y, w, h, nullptr, nullptr, inst, nullptr);

  auto make = [&](const wchar_t* cls2, const wchar_t* text, DWORD style, int cx, int cy, int cw, int ch,
                  int id) {
    HWND h = CreateWindowExW(0, cls2, text, WS_CHILD | WS_VISIBLE | style, cx, cy, cw, ch, hwnd,
                             (HMENU)(INT_PTR)id, inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    return h;
  };

  make(L"STATIC", L"OrangeM", SS_LEFT, 32, 24, 200, 30, 0);
  HWND brand = GetDlgItem(hwnd, 0);
  (void)brand;
  make(L"STATIC", L"Orange ID — единый аккаунт во всех продуктах", SS_LEFT, 32, 54, 380, 20, 0);
  make(L"STATIC", L"Сервер", SS_LEFT, 32, 92, 200, 16, 0);
  HWND srv = make(L"EDIT", L"", ES_AUTOHSCROLL, 32, 112, 396, 26, ID_SERVER);
  SetWindowTextW(srv, u2w(api().serverUrl()).c_str());
  make(L"STATIC", L"Почта, телефон или Orange ID", SS_LEFT, 32, 150, 300, 16, 0);
  make(L"EDIT", L"", ES_AUTOHSCROLL, 32, 170, 396, 26, ID_IDENT);
  make(L"STATIC", L"Пароль или код аутентификатора", SS_LEFT, 32, 208, 320, 16, 0);
  make(L"EDIT", L"", ES_PASSWORD | ES_AUTOHSCROLL, 32, 228, 396, 26, ID_PASS);
  make(L"BUTTON", L"Войти", BS_OWNERDRAW, 32, 276, 190, 36, ID_LOGIN);
  make(L"BUTTON", L"Войти по флеш-ключу", BS_OWNERDRAW, 238, 276, 190, 36, ID_FLASH);
  make(L"BUTTON", L"Регистрация", BS_OWNERDRAW, 32, 322, 396, 30, ID_REG);
  make(L"STATIC", L"", SS_LEFT, 32, 364, 396, 40, ID_STATUS);
  return hwnd;
}

// ------------------------------------------------------------------ самотест (без GUI)
static int pass = 0, fail = 0;
static void chk(const char* name, bool cond, const std::string& extra = "") {
  if (cond) { pass++; printf("  PASS %s\n", name); }
  else { fail++; printf("  FAIL %s%s%s\n", name, extra.empty() ? "" : " -- ", extra.c_str()); }
  fflush(stdout);
}

static int runSelfTest(const std::string& server, const std::string& user, const std::string& password) {
  printf("== OrangeM Desktop self-test ==\n");
  api().http.baseUrl = server;
  std::string err;

  HttpResponse h = api().http.get("/api/health");
  chk("сервер отвечает /api/health", h.ok(), h.errorText());

  long long ts = nowSec();
  std::string uname = "dt" + std::to_string(ts % 1000000);
  std::string email = uname + "@example.com";
  std::string target, devCode;
  bool reg = api().registerStart("email", email, uname, "DesktopPass123", target, devCode, err);
  chk("регистрация: запрос кода", reg, err);
  if (reg) {
    bool v = api().registerVerify(target, devCode, err);
    chk("регистрация: подтверждение кода", v, err);
  }
  if (!api().http.token.empty()) {
    bool me = api().loadMe(err);
    chk("профиль /api/me", me && !api().me.orangeId.empty(), err);
    chk("Orange ID получен", api().me.orangeId.rfind("OM-", 0) == 0, api().me.orangeId);
  } else if (!user.empty()) {
    bool okLogin = api().loginPassword(user, password, err);
    chk("вход по логину/паролю", okLogin, err);
    if (okLogin && !api().needTotp) chk("профиль /api/me", api().loadMe(err), err);
  }

  std::string sfRot, sfErr;
  HttpResponse sfc = api().http.post("/api/auth/session-file/create", Json::obj().set("label", "Desktop"));
  chk("создание флеш-ключа", sfc.ok() && !sfc.json()["content"].str().empty(), sfc.errorText());
  std::string kf = sfc.json()["content"].str();
  std::string sfToken = api().http.token;
  api().http.token.clear();
  bool sfOk = api().loginSessionFile(kf, sfRot, sfErr);
  chk("вход по флеш-ключу", sfOk, sfErr);
  chk("ключ перешифрован сервером", !sfRot.empty() && sfRot != kf);
  api().http.token = sfToken;

  printf("  .. запрос списка чатов\n"); fflush(stdout);
  auto chats = api().chats(err);
  printf("  .. чатов получено: %d\n", (int)chats.size()); fflush(stdout);
  chk("список чатов", err.empty(), err);

  printf("  .. запрос ленты\n"); fflush(stdout);
  auto posts = api().feed("global", err);
  printf("  .. лента получена: %d, err=%s\n", (int)posts.size(), err.c_str()); fflush(stdout);
  chk("лента", err.empty(), err);

  std::string postErr;
  bool madePost = api().createPost("Desktop проверка " + std::to_string(ts), "Пост из Windows-клиента", postErr);
  chk("публикация из клиента", madePost, postErr);
  auto posts2 = api().feed("global", err);
  chk("публикация видна в ленте", !posts2.empty() &&
      posts2[0].title.find("Desktop проверка") != std::string::npos,
      posts2.empty() ? "лента пуста" : posts2[0].title);
  if (!posts2.empty()) {
    std::string likeErr;
    bool liked = api().likePost(posts2[0].id, true, likeErr);
    chk("лайк публикации", liked, likeErr);
    std::string cmtErr;
    chk("комментарий к публикации", api().commentPost(posts2[0].id, "Комментарий из клиента", cmtErr), cmtErr);
  }

  auto comms = api().communities("all", err);
  chk("список сообществ", err.empty() && !comms.empty(), err);

  // realtime: подключаемся и проверяем приход события
  std::string wsErr;
  static std::atomic<int> gotReady{0};
  static std::atomic<int> gotTyping{0};
  api().onEvent = [](const Json& j) {
    std::string t = j["type"].str();
    if (t == "ready") gotReady++;
    if (t == "typing") gotTyping++;
  };
  bool ws = api().startRealtime(wsErr);
  chk("подключение websocket", ws, wsErr);
  for (int i = 0; i < 60 && gotReady.load() == 0; i++) Sleep(50);
  chk("событие ready получено", gotReady.load() > 0);

  // личный чат и сообщение
  long long chatId = 0;
  std::string dmErr;
  auto found = api().searchUsers("admin", err);
  std::string peer = found.empty() ? "" : found[0].username;
  if (!peer.empty() && api().openDm(peer, chatId, dmErr)) {
    chk("создание личного чата", chatId != 0);
    Msg m;
    std::string sendErr;
    bool sent = api().sendMessage(chatId, "Сообщение из Windows-клиента", m, sendErr);
    chk("отправка сообщения", sent, sendErr);
    auto msgs = api().messages(chatId, err);
    chk("история сообщений", !msgs.empty() &&
        msgs.back().body.find("Windows-клиента") != std::string::npos,
        msgs.empty() ? "пусто" : msgs.back().body);
    chk("прочтение отмечено", api().markRead(chatId, msgs.empty() ? 0 : msgs.back().id));
  } else {
    chk("создание личного чата", false, dmErr);
  }

  auto stories = api().stories(err);
  chk("истории", err.empty(), err);
  chk("публикация истории", api().createStory("История из Windows-клиента", "#f97316", err), err);

  api().stopRealtime();
  printf("\n== Desktop: PASS=%d FAIL=%d ==\n", pass, fail);
  return fail ? 1 : 0;
}

// ------------------------------------------------------------------ демо-режим (проверка интерфейса без сервера)
static void fillDemoData(int section) {
  std::lock_guard<std::mutex> lk(g_shared.m);
  ui::UiState& s = g_shared.st;
  s.section = section;
  s.serverUrl = api().serverUrl();
  s.realtime = true;
  s.status = "Демо-режим: данные для проверки интерфейса";
  s.me.id = 1;
  s.me.orangeId = "OM-7K2QF3XD";
  s.me.username = "admin";
  s.me.displayName = "Администратор OrangeM";
  s.me.bio = "Единый аккаунт во всех продуктах OrangeM.";
  s.me.followers = 128;
  s.me.following = 42;
  s.me.posts = 17;
  s.me.isAdmin = true;
  s.me.online = true;

  const char* names[] = {"Анна Кузнецова", "Дмитрий Соколов", "Мария Волкова", "Игорь Лебедев",
                         "OrangeM Support", "Тестовая группа"};
  const char* previews[] = {"Отправила документы по проекту", "Созвон в 15:00 подтверждён",
                            "Спасибо за помощь!", "Посмотри ветку feature/realtime",
                            "Плановые работы в 02:00", "Игорь: добавил описание"};
  for (int i = 0; i < 6; i++) {
    ChatItem c;
    c.id = 100 + i;
    c.kind = i == 5 ? "group" : "dm";
    c.title = names[i];
    c.preview = previews[i];
    c.lastAt = nowSec() - (i + 1) * 420;
    c.unread = i < 2 ? (i + 1) : 0;
    c.peerOnline = i % 2 == 0;
    c.pinned = i == 0;
    c.muted = i == 4;
    c.peerId = 200 + i;
    c.peerUsername = i == 5 ? "team" : "user" + std::to_string(i + 1);
    c.peerOrangeId = "OM-DEMO" + std::to_string(1000 + i);
    s.chats.push_back(c);
  }
  const char* msgs[] = {
      "Привет! Как продвигается интеграция OrangeM?",
      "Привет. Сервер на C++ уже собирается, осталось причесать интерфейс.",
      "Отлично. Строгая тема выглядит лучше — без эмодзи и градиентов.",
      "Да, палитра нейтральная, акцент один — оранжевый.",
      "Когда будет десктопный клиент?",
      "Уже запускается: Win32, GDI+, realtime через WebSocket.",
      "Проверил флеш-ключ — ротация в реальном времени работает.",
      "Ещё нужно проверить вход по коду аутентификатора.",
      "TOTP проверен, QR генерируется на клиенте.",
      "Красота. Тогда готовим релиз 1.0.",
      "Осталось прогнать нагрузочный тест и обновить документацию.",
      "Согласен, план принят."};
  for (int i = 0; i < 12; i++) {
    Msg m;
    m.id = 500 + i;
    m.chatId = 100;
    m.senderId = (i % 2 == 0) ? 200 : 1;
    m.mine = (i % 2 != 0);
    m.body = msgs[i];
    m.createdAt = nowSec() - (12 - i) * 260;
    m.senderName = m.mine ? "Администратор OrangeM" : "Анна Кузнецова";
    m.reads = m.mine ? 1 : 0;
    s.msgs.push_back(m);
  }
  s.activeChat = 100;
  s.chattingWith = "Анна Кузнецова";
  s.peerOnline = true;

  const char* titles[] = {"OrangeM 1.0: строгий интерфейс", "Как устроен флеш-ключ",
                          "Realtime на WebSocket", "Сообщества: группы и каналы",
                          "Orange ID во всех продуктах"};
  const char* bodies[] = {
      "Мы убрали эмодзи и градиенты, оставили один акцент и плотную типографику. #design #orangem",
      "Файл ключа перешифровывается при каждом входе: старый секрет сразу становится недействительным. #security",
      "События message.new, typing и message.read приходят мгновенно, без перезагрузки. #realtime",
      "Внутри сообщества есть собственный чат, роли и приглашения по коду. #community",
      "Один идентификатор OM-XXXXXXXX работает и в ленте, и в чатах, и в будущих сервисах. #orangeid"};
  for (int i = 0; i < 5; i++) {
    Post p;
    p.id = 900 + i;
    p.title = titles[i];
    p.body = bodies[i];
    p.authorId = i % 2 ? 201 : 1;
    p.authorName = i % 2 ? "Дмитрий Соколов" : "Администратор OrangeM";
    p.authorUsername = i % 2 ? "sokolov" : "admin";
    p.likes = 12 + i * 3;
    p.comments = i;
    p.views = 120 + i * 11;
    p.createdAt = nowSec() - (i + 1) * 5400;
    p.liked = i % 3 == 0;
    p.isMine = i % 2 == 0;
    p.tags.push_back("orangem");
    s.posts.push_back(p);
  }

  const char* commNames[] = {"OrangeM Клуб", "C++ разработка", "Дизайн интерфейсов", "Новости продукта"};
  const char* commDesc[] = {"Обсуждение мессенджера, релизов и планов.",
                            "Практика современного C++17 и сетевого программирования.",
                            "Строгие интерфейсы, дизайн-системы, типографика.",
                            "Официальные анонсы OrangeM."};
  for (int i = 0; i < 4; i++) {
    Community c;
    c.id = 300 + i;
    c.name = commNames[i];
    c.slug = "community-" + std::to_string(i + 1);
    c.description = commDesc[i];
    c.kind = i == 3 ? "channel" : "group";
    c.members = 1240 - i * 137;
    c.isMember = i < 2;
    c.isPublic = true;
    c.orangeId = "OMG-DEMO" + std::to_string(i + 1);
    c.ownerName = "Администратор OrangeM";
    c.chatId = 700 + i;
    s.communities.push_back(c);
  }

  const char* storyNames[] = {"Анна Кузнецова", "Дмитрий Соколов", "Администратор OrangeM"};
  const char* storyCaptions[] = {"Новый релиз уже в проде", "Разбираю код сервера", "Строгая тема готова"};
  for (int i = 0; i < 3; i++) {
    StoryGroup g;
    g.id = 1500 + i;
    g.userId = 200 + i;
    g.name = storyNames[i];
    g.caption = storyCaptions[i];
    g.kind = "text";
    g.background = i % 2 ? "#3b82f6" : "#f97316";
    g.hasUnseen = i < 2;
    g.count = 2 + i;
    g.views = 34 + i * 7;
    g.createdAt = nowSec() - (i + 1) * 900;
    s.stories.push_back(g);
  }
}

// ------------------------------------------------------------------ точка входа
static std::string argValue(const std::wstring& cmdline, const std::wstring& key) {
  size_t pos = cmdline.find(key + L"=");
  if (pos == std::wstring::npos) return "";
  pos += key.size() + 1;
  size_t end = cmdline.find(L' ', pos);
  std::wstring v = cmdline.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
  if (!v.empty() && v.front() == L'"') v.erase(v.begin());
  if (!v.empty() && v.back() == L'"') v.pop_back();
  return w2u(v);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
  g_inst = inst;
  std::wstring cmd = GetCommandLineW();
  g_selftest = cmd.find(L"--selftest") != std::wstring::npos;

  if (g_selftest) {
    // Никаких дополнительных окон: если stdout передан (запуск из консоли/скрипта) — пишем в него,
    // иначе результаты уходят в файл selftest.log рядом с настройками (окно консоли не создаём).
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    bool hasOut = hOut && hOut != INVALID_HANDLE_VALUE;
    std::string logPath = appDataDir() + "\\selftest.log";
    if (!hasOut) {
      FILE* f = nullptr;
      freopen_s(&f, logPath.c_str(), "w", stdout);
      freopen_s(&f, logPath.c_str(), "w", stderr);
    } else {
      SetConsoleOutputCP(CP_UTF8);
    }
    Gdiplus::GdiplusStartupInput gdiIn;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiIn, nullptr);
    api().loadSettings();
    std::string server = argValue(cmd, L"--server");
    if (server.empty()) server = api().serverUrl();
    int rc = runSelfTest(server, argValue(cmd, L"--user"), argValue(cmd, L"--password"));
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (!hasOut) {
      // результат виден в файле; открывать консоль или диалог не нужно
      fflush(stdout);
    }
    return rc;
  }

  INITCOMMONCONTROLSEX icc;
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&icc);

  Gdiplus::GdiplusStartupInput gdiIn;
  Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiIn, nullptr);

  makeFonts();
  theme::initFonts();
  api().loadSettings();

  // колбэки интерфейса -> рабочая нить
  ui::UiCallbacks cb;
  cb.onSection = [](int s) { g_worker.push({CMD_SECTION, s, false, ""}); };
  cb.onChatSelected = [](long long id) {
    std::string title;
    {
      std::lock_guard<std::mutex> lk(g_shared.m);
      for (auto& c : g_shared.st.chats) if (c.id == id) title = c.title;
    }
    g_worker.push({CMD_OPEN_CHAT, id, false, title});
  };
  cb.onSendMessage = [](const std::string& text) {
    long long id = 0;
    { std::lock_guard<std::mutex> lk(g_shared.m); id = g_shared.st.activeChat; }
    if (id) g_worker.push({CMD_SEND, id, false, text});
  };
  cb.onCreatePost = [](const std::string& text) { g_worker.push({CMD_CREATE_POST, 0, false, text}); };
  cb.onSearch = [](const std::string& q) { g_worker.push({CMD_SEARCH, 0, false, q}); };
  cb.onSearchCleared = [] { g_worker.push({CMD_SEARCH, 0, false, ""}); };
  cb.onToggleCommunity = [](long long id, bool join) { g_worker.push({CMD_TOGGLE_COMMUNITY, id, join, ""}); };
  cb.onOpenCommunityChat = [](long long id) { g_worker.push({CMD_OPEN_CHAT, id, false, "Чат сообщества"}); };
  cb.onLikePost = [](long long id, bool like) { g_worker.push({CMD_LIKE, id, like, ""}); };
  cb.onCommentPost = [](long long id, const std::string& text) { g_worker.push({CMD_COMMENT, id, false, text}); };
  cb.onAttach = [] {
    long long id = 0;
    { std::lock_guard<std::mutex> lk(g_shared.m); id = g_shared.st.activeChat; }
    if (!id) { ui::showToast("Сначала откройте чат"); return; }
    wchar_t file[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"Изображения\0*.png;*.jpg;*.jpeg;*.gif;*.webp\0Все файлы\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) g_worker.push({CMD_ATTACH, id, false, w2u(file)});
  };
  cb.onNewChat = [] {
    // простой ввод идентификатора через диалог
    wchar_t buf[128] = {0};
    // используем поле поиска как источник: если пусто — подсказка в статусе
    std::string q;
    { std::lock_guard<std::mutex> lk(g_shared.m); q = g_shared.st.search; }
    if (q.empty()) { ui::showToast("Введите @username, e-mail или Orange ID в поиск и нажмите «Новый чат»"); return; }
    (void)buf;
    g_worker.push({CMD_NEW_CHAT, 0, false, q});
  };
  cb.onRefresh = [] { g_worker.push({CMD_REFRESH, 0, false, ""}); ui::showToast("Обновление…"); };
  cb.onLogout = [] { g_worker.push({CMD_LOGOUT, 0, false, ""}); };
  cb.onViewStory = [](long long id) { g_worker.push({CMD_VIEW_STORY, id, false, ""}); };
  cb.onCreateStory = [] { g_worker.push({CMD_CREATE_STORY, 0, false, "История из OrangeM для Windows"}); };
  cb.onServerChanged = [](const std::string& url) { api().setServerUrl(url); };
  ui::setCallbacks(cb);
  ui::setAppMessageHandler(appMessage);

  g_worker.start();

  // авто-вход при сохранённой сессии
  bool restored = false;
  if (api().loadSession()) {
    std::string err;
    if (api().loadMe(err)) restored = true;
    else api().clearSession();
  }

  if (restored) {
    enterApp();
  } else if (cmd.find(L"--demo") != std::wstring::npos) {
    // Демонстрационный режим: интерфейс наполняется тестовыми данными без сервера.
    int section = ui::SEC_CHATS;
    std::string sec = argValue(cmd, L"--section");
    if (sec == "feed") section = ui::SEC_FEED;
    else if (sec == "communities") section = ui::SEC_COMMUNITIES;
    else if (sec == "stories") section = ui::SEC_STORIES;
    else if (sec == "profile") section = ui::SEC_PROFILE;
    fillDemoData(section);
    g_main = ui::createMainWindow(g_inst, L"OrangeM — демо-режим");
    ShowWindow(g_main, SW_SHOWMAXIMIZED);
    UpdateWindow(g_main);
    {
      std::lock_guard<std::mutex> lk(g_shared.m);
      ui::setState(g_shared.st);
    }
    ui::invalidate();
  } else {
    g_login = createLoginWindow(inst);
    ShowWindow(g_login, SW_SHOW);
    UpdateWindow(g_login);
    SetFocus(GetDlgItem(g_login, ID_IDENT));
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (g_main && IsDialogMessageW(g_main, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g_worker.stop();
  api().stopRealtime();
  Gdiplus::GdiplusShutdown(g_gdiplusToken);
  return 0;
}
