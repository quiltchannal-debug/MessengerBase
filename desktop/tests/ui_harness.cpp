// OrangeM Desktop - тестовый стенд интерфейса (НЕ входит в сборку клиента).
// Файл включает ../src/ui.cpp напрямую, чтобы иметь доступ к реестру попаданий (g_hits)
// и проверять отрисовку/взаимодействие без живого сервера.
#include "../src/ui.cpp"

#include <cstdio>

using namespace om;
using namespace om::ui;

static int g_pass = 0, g_fail = 0;

static void chk(const char* name, bool ok, const std::string& extra = "") {
  if (ok) {
    g_pass++;
    printf("PASS %s\n", name);
  } else {
    g_fail++;
    printf("FAIL %s%s%s\n", name, extra.empty() ? "" : " -- ", extra.c_str());
  }
  fflush(stdout);
}

// ---- запись вызовов колбэков ----
struct Rec {
  int section = -1;
  long long chat = 0;
  std::string send, post, search, server, comment;
  bool attach = false, refresh = false, logout = false, newChat = false, storyAdd = false;
  bool searchCleared = false;
  long long liked = 0;
  bool likeVal = false;
  long long commented = 0;
  long long viewed = 0;
  long long commId = 0;
  bool commJoinVal = false;
  long long commChat = 0;
};
static Rec g_rec;
static UiState g_st;

static void pump() {
  MSG m;
  int guard = 0;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE) && guard++ < 5000) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

static void repaint() {
  HWND h = ui::mainWindow();
  ui::invalidate();
  UpdateWindow(h);
  pump();
}

static const Hit* findHit(HitKind k, long long id = -1, int act = -1) {
  repaint();
  for (size_t i = 0; i < om::ui::g_hits.size(); i++) {
    const Hit& h = om::ui::g_hits[i];
    if (h.kind != k) continue;
    if (id != -1 && h.id != id) continue;
    if (act != -1 && h.act != act) continue;
    return &h;
  }
  return nullptr;
}

static bool clickHit(HitKind k, long long id = -1, int act = -1) {
  const Hit* h = findHit(k, id, act);
  if (!h) return false;
  int x = (h->rc.left + h->rc.right) / 2;
  int y = (h->rc.top + h->rc.bottom) / 2;
  SendMessageW(ui::mainWindow(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
  pump();
  return true;
}

static std::wstring textOf(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring s((size_t)n + 1, L'\0');
  GetWindowTextW(h, &s[0], n + 1);
  s.resize((size_t)n);
  return s;
}

static void setCtrl(bool down) {
  BYTE keys[256];
  GetKeyboardState(keys);
  keys[VK_CONTROL] = down ? 0x80 : 0;
  SetKeyboardState(keys);
}

static void sendKey(HWND h, int vk) {
  SendMessageW(h, WM_KEYDOWN, (WPARAM)vk, 0);
  SendMessageW(h, WM_KEYUP, (WPARAM)vk, 0);
  pump();
}

// ---- захват окна в PNG ----
static int getEncoderClsid(const WCHAR* mime, CLSID* clsid) {
  UINT num = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (size == 0) return -1;
  std::vector<BYTE> buf(size);
  Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)&buf[0];
  Gdiplus::GetImageEncoders(num, size, info);
  for (UINT i = 0; i < num; i++) {
    if (wcscmp(info[i].MimeType, mime) == 0) {
      *clsid = info[i].Clsid;
      return (int)i;
    }
  }
  return -1;
}

static bool shot(const wchar_t* file, int& colors, int& orange) {
  HWND h = ui::mainWindow();
  RECT rc;
  GetClientRect(h, &rc);
  int w = rc.right, ht = rc.bottom;
  if (w <= 0 || ht <= 0) return false;
  HDC wdc = GetDC(h);
  HDC mem = CreateCompatibleDC(wdc);
  HBITMAP bmp = CreateCompatibleBitmap(wdc, w, ht);
  HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, w, ht, wdc, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  ReleaseDC(h, wdc);
  DeleteDC(mem);

  Gdiplus::Bitmap gb(bmp, nullptr);
  colors = 0;
  orange = 0;
  {
    std::map<std::string, int> seen;
    for (int y = 0; y < ht; y += 4) {
      for (int x = 0; x < w; x += 4) {
        Gdiplus::Color c;
        gb.GetPixel(x, y, &c);
        char key[32];
        snprintf(key, sizeof(key), "%u,%u,%u", (unsigned)c.GetR(), (unsigned)c.GetG(),
                 (unsigned)c.GetB());
        seen[key] = 1;
        if (c.GetR() > 180 && c.GetG() > 80 && c.GetG() < 190 && c.GetB() < 90) orange++;
      }
    }
    colors = (int)seen.size();
  }
  CLSID clsid;
  bool ok = false;
  if (getEncoderClsid(L"image/png", &clsid) >= 0) {
    ok = (gb.Save(file, &clsid, nullptr) == Gdiplus::Ok);
  }
  DeleteObject(bmp);
  return ok;
}

// ---- синтетические данные ----
static void fillState(UiState& s) {
  s.section = SEC_CHATS;
  s.activeChat = 1;
  s.chattingWith = "Анна Соколова";
  s.peerOnline = true;
  s.realtime = true;
  s.loading = false;
  s.status = "Готово";
  s.serverUrl = "http://213.108.1.226:8080";

  s.me.id = 7;
  s.me.displayName = "Екатерина Орлова";
  s.me.username = "ekaterina";
  s.me.orangeId = "OM-7F3C-91A2";
  s.me.bio = "Разработка OrangeM: десктопный клиент, сервер, дизайн-система.";
  s.me.followers = 128;
  s.me.following = 42;
  s.me.posts = 17;
  s.me.online = true;

  const char* titles[6] = {"Анна Соколова", "Команда OrangeM", "Дмитрий Ковалёв",
                           "Мария Лебедева", "Служба поддержки", "Игорь Петров"};
  const char* prevs[6] = {"Созвон в 15:00 подтверждаю",
                          "Собрали новый билд клиента",
                          "Отправил документы на почту",
                          "Посмотри макет, пожалуйста",
                          "Ваш запрос принят в работу",
                          "Спасибо!"};
  for (int i = 0; i < 6; i++) {
    ChatItem c;
    c.id = i + 1;
    c.kind = i == 1 ? "group" : "dm";
    c.title = titles[i];
    c.preview = prevs[i];
    c.lastAt = 1750000000 + i * 3600;
    c.unread = i < 3 ? (i == 0 ? 12 : i) : 0;
    c.pinned = (i == 1);
    c.muted = (i == 4);
    c.peerId = 100 + i;
    c.peerOnline = (i % 2 == 0);
    c.peerUsername = "user";
    c.peerOrangeId = "OM-1111-2222";
    s.chats.push_back(c);
  }

  Msg m;
  m.id = 1; m.chatId = 1; m.senderId = 7; m.mine = true;
  m.body = "Привет! Посмотрела новый макет — выглядит строго и аккуратно.";
  m.createdAt = 1750000100;
  s.msgs.push_back(m);
  m.id = 2; m.mine = false; m.senderId = 100; m.senderName = "Анна Соколова";
  m.body = "Отлично. Тогда переносим в клиент и проверяем на Windows.";
  m.createdAt = 1750000200;
  s.msgs.push_back(m);
  m.id = 3; m.mine = false; m.senderId = 0; m.system = true;
  m.body = "Сообщения защищены сквозным шифрованием";
  m.createdAt = 1750000250;
  s.msgs.push_back(m);
  m.id = 4; m.mine = true; m.senderId = 7; m.system = false;
  m.body = "Отправила сборку, прикрепила скриншот интерфейса чата.";
  m.attachment = "https://example.org/a.png";
  m.attachmentKind = "image";
  m.createdAt = 1750000300;
  s.msgs.push_back(m);
  m.system = false;
  m.attachment.clear();

  for (int i = 0; i < 3; i++) {
    Post p;
    p.id = i + 1;
    p.authorId = 7;
    p.authorName = i == 2 ? "Команда OrangeM" : "Екатерина Орлова";
    p.authorUsername = "ekaterina";
    p.communityName = "OrangeM";
    p.title = i == 0 ? "Строгий интерфейс десктопного клиента" : "";
    p.body =
        "Собрали клиент на Win32 и GDI+: строгая тёмная тема, тонкие границы, один оранжевый акцент. "
        "Никаких эмодзи и градиентов — только системные шрифты и аккуратные отступы.";
    p.createdAt = 1750000400 + i * 600;
    p.likes = 12 + i * 3;
    p.comments = 2 + i;
    p.views = 340 + i * 21;
    p.liked = (i == 1);
    p.isMine = (i != 2);
    p.tags.push_back("orangem");
    p.tags.push_back("design");
    if (i == 0) p.tags.push_back("windows");
    s.posts.push_back(p);
  }

  const char* cnames[3] = {"OrangeM", "Разработка", "Дизайн-система"};
  const char* cdescs[3] = {"Официальное сообщество проекта: новости, сборки, обсуждения.",
                           "Инженерные вопросы, код-ревью и релизный цикл.",
                           "Палитра, метрики, иконки и правила оформления."};
  for (int i = 0; i < 3; i++) {
    Community c;
    c.id = i + 1;
    c.name = cnames[i];
    c.slug = "orangem";
    c.description = cdescs[i];
    c.kind = i == 1 ? "channel" : "group";
    c.members = 120 + i * 45;
    c.chatId = 100 + i;
    c.isMember = (i == 0);
    c.ownerName = "Екатерина Орлова";
    s.communities.push_back(c);
  }

  const char* snames[5] = {"Анна", "Дмитрий", "Мария", "Игорь", "Ольга"};
  for (int i = 0; i < 5; i++) {
    StoryGroup g;
    g.id = i + 1;
    g.userId = 200 + i;
    g.name = snames[i];
    g.caption = "Новости дня";
    g.count = 1 + i;
    g.hasUnseen = (i < 3);
    g.views = 10 * i;
    g.createdAt = 1750000500;
    s.stories.push_back(g);
  }
}

int wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
  FILE* logf = nullptr;
  freopen_s(&logf, "D:\\messenger\\desktop\\tests\\harness.log", "w", stdout);
  if (logf) setvbuf(logf, nullptr, _IONBF, 0);
  ULONG_PTR token = 0;
  Gdiplus::GdiplusStartupInput gin;
  Gdiplus::GdiplusStartup(&token, &gin, nullptr);
  INITCOMMONCONTROLSEX icc;
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&icc);

  theme::initFonts();
  printf("== OrangeM UI harness ==\n");

  // ---- колбэки ----
  UiCallbacks cb;
  cb.onSection = [](int s) { g_rec.section = s; g_st.section = s; ui::setState(g_st); };
  cb.onChatSelected = [](long long id) {
    g_rec.chat = id;
    g_st.activeChat = id;
    g_st.chattingWith = "Чат " + std::to_string(id);
    ui::setState(g_st);
  };
  cb.onSendMessage = [](const std::string& t) { g_rec.send = t; };
  cb.onCreatePost = [](const std::string& t) { g_rec.post = t; };
  cb.onSearch = [](const std::string& q) { g_rec.search = q; };
  cb.onSearchCleared = [] { g_rec.searchCleared = true; };
  cb.onToggleCommunity = [](long long id, bool join) { g_rec.commId = id; g_rec.commJoinVal = join; };
  cb.onOpenCommunityChat = [](long long id) { g_rec.commChat = id; };
  cb.onLikePost = [](long long id, bool like) { g_rec.liked = id; g_rec.likeVal = like; };
  cb.onCommentPost = [](long long id, const std::string& t) { g_rec.commented = id; g_rec.comment = t; };
  cb.onAttach = [] { g_rec.attach = true; };
  cb.onNewChat = [] { g_rec.newChat = true; };
  cb.onRefresh = [] { g_rec.refresh = true; };
  cb.onLogout = [] { g_rec.logout = true; };
  cb.onViewStory = [](long long id) { g_rec.viewed = id; };
  cb.onCreateStory = [] { g_rec.storyAdd = true; };
  cb.onServerChanged = [](const std::string& u) { g_rec.server = u; };
  ui::setCallbacks(cb);

  fillState(g_st);
  ui::setState(g_st);

  HWND h = ui::createMainWindow(inst, L"OrangeM");
  chk("окно создано", h != nullptr);
  if (!h) {
    Gdiplus::GdiplusShutdown(token);
    return 1;
  }
  ShowWindow(h, SW_SHOW);
  SetForegroundWindow(h);
  repaint();

  RECT cr;
  GetClientRect(h, &cr);
  printf("INFO клиентская область %ldx%ld\n", cr.right, cr.bottom);

  // ---- 1. отрисовка всех разделов ----
  int colors = 0, orange = 0;
  bool shotOk = false;
  for (int sec = SEC_CHATS; sec <= SEC_PROFILE; sec++) {
    g_st.section = sec;
    g_st.activeChat = (sec == SEC_CHATS) ? 1 : g_st.activeChat;
    ui::setState(g_st);
    repaint();
    wchar_t file[128];
    wsprintfW(file, L"D:\\messenger\\desktop\\tests\\shot-%d.png", sec);
    colors = 0; orange = 0;
    shotOk = shot(file, colors, orange);
    const char* names[5] = {"Чаты", "Лента", "Сообщества", "Истории", "Настройки"};
    printf("INFO раздел %-11s цветов %5d, оранжевых %5d\n", names[sec], colors, orange);
    chk((std::string("отрисован раздел ") + names[sec]).c_str(), shotOk && colors >= 20);
    chk((std::string("акцент в разделе ") + names[sec]).c_str(), orange >= 20,
        "оранжевых " + std::to_string(orange));
  }

  // ---- 2. навигация по сайдбару ----
  g_st.section = SEC_CHATS;
  ui::setState(g_st);
  repaint();
  struct SecCase { int sec; const char* name; };
  SecCase cases[5] = {{SEC_FEED, "Лента"}, {SEC_CHATS, "Чаты"}, {SEC_COMMUNITIES, "Сообщества"},
                      {SEC_STORIES, "Истории"}, {SEC_PROFILE, "Настройки"}};
  for (int i = 0; i < 5; i++) {
    g_rec.section = -1;
    bool clicked = clickHit(HK_SECTION, cases[i].sec);
    chk((std::string("клик по пункту «") + cases[i].name + "»").c_str(),
        clicked && g_rec.section == cases[i].sec,
        "clicked=" + std::to_string((int)clicked) + " section=" + std::to_string(g_rec.section));
    repaint();
  }

  // ---- 3. чаты ----
  g_st.section = SEC_CHATS;
  g_st.activeChat = 1;
  ui::setState(g_st);
  repaint();

  g_rec.chat = 0;
  chk("клик по строке чата", clickHit(HK_CHAT, 3) && g_rec.chat == 3,
      "chat=" + std::to_string(g_rec.chat));
  repaint();

  HWND msgEdit = GetDlgItem(h, 2002);
  chk("поле ввода сообщения создано", msgEdit != nullptr);
  if (msgEdit) {
    SetWindowTextW(msgEdit, L"Проверка отправки");
    g_rec.send.clear();
    bool clicked = clickHit(HK_ACTION, -1, ACT_SEND);
    chk("кнопка «отправить» → onSendMessage", clicked && g_rec.send == "Проверка отправки",
        "send='" + g_rec.send + "'");
    chk("поле ввода очищено после отправки", msgEdit && textOf(msgEdit).empty());
    g_rec.send.clear();
    SetWindowTextW(msgEdit, L"Через Enter");
    sendKey(msgEdit, VK_RETURN);
    chk("Enter в поле ввода → onSendMessage", g_rec.send == "Через Enter", "send='" + g_rec.send + "'");
  }

  chk("ui::messageInputText/setMessageInputText", [&] {
    ui::setMessageInputText(L"текст");
    return ui::messageInputText() == L"текст";
  }());
  ui::focusMessageInput();
  chk("focusMessageInput без падения", true);

  g_rec.attach = false;
  chk("кнопка «вложение» → onAttach", clickHit(HK_ACTION, -1, ACT_ATTACH) && g_rec.attach);
  g_rec.refresh = false;
  chk("кнопка «обновить» → onRefresh", clickHit(HK_ACTION, -1, ACT_REFRESH) && g_rec.refresh);
  g_rec.newChat = false;
  chk("hits: реестр попаданий не пуст", om::ui::g_hits.size() > 5,
      "hits=" + std::to_string(om::ui::g_hits.size()));

  // ---- 4. профиль ----
  g_st.section = SEC_PROFILE;
  ui::setState(g_st);
  repaint();
  g_rec.logout = false;
  chk("кнопка «Выйти» → onLogout", clickHit(HK_ACTION, -1, ACT_LOGOUT) && g_rec.logout);
  g_rec.refresh = false;
  chk("кнопка «Обновить» в профиле", clickHit(HK_ACTION, -1, ACT_REFRESH) && g_rec.refresh);
  HWND srv = GetDlgItem(h, 2004);
  chk("поле адреса сервера создано", srv != nullptr);
  if (srv) {
    SetWindowTextW(srv, L"http://localhost:8080");
    sendKey(srv, VK_RETURN);
    chk("Enter в адресе сервера → onServerChanged", g_rec.server == "http://localhost:8080",
        "server='" + g_rec.server + "'");
  }
  chk("ui::setStatus", [&] { ui::setStatus("тест"); return ui::state().status == "тест"; }());

  // ---- 5. лента ----
  g_st.section = SEC_FEED;
  ui::setState(g_st);
  repaint();
  HWND cedit = GetDlgItem(h, 2003);
  chk("композер создан", cedit != nullptr);
  if (cedit) {
    SetWindowTextW(cedit, L"Новая публикация");
    g_rec.post.clear();
    bool clicked = clickHit(HK_ACTION, -1, ACT_PUBLISH);
    chk("кнопка «Опубликовать» → onCreatePost", clicked && g_rec.post == "Новая публикация",
        "post='" + g_rec.post + "'");
  }
  g_rec.liked = 0;
  bool likeClicked = clickHit(HK_POST_LIKE);
  chk("кнопка «сердце» → onLikePost", likeClicked && g_rec.liked != 0,
      "liked=" + std::to_string(g_rec.liked) + " val=" + std::to_string((int)g_rec.likeVal));
  g_rec.commented = 0;
  bool commentClicked = clickHit(HK_POST_COMMENT);
  chk("кнопка «комментарий» открывает модальное окно", commentClicked && om::ui::g_commentWnd != nullptr);
  if (om::ui::g_commentWnd) {
    SetWindowTextW(om::ui::g_commentEdit, L"Комментарий из стенда");
    SendMessageW(om::ui::g_commentWnd, WM_COMMAND, MAKEWPARAM(2006, BN_CLICKED), 0);
    pump();
    chk("модальное окно → onCommentPost и закрытие",
        g_rec.commented != 0 && g_rec.comment == "Комментарий из стенда" &&
            om::ui::g_commentWnd == nullptr,
        "id=" + std::to_string(g_rec.commented) + " text='" + g_rec.comment + "'");
  }
  chk("кнопка «ссылка» копирует ссылку", clickHit(HK_POST_LINK));
  g_rec.liked = 0;
  chk("ui::showToast не падает", [&] { ui::showToast("Проверка тоста"); repaint(); return true; }());

  // поиск в ленте
  HWND se = GetDlgItem(h, 2001);
  chk("поле поиска создано", se != nullptr);
  if (se) {
    SetWindowTextW(se, L"дизайн");
    g_rec.search.clear();
    sendKey(se, VK_RETURN);
    chk("Enter в поиске → onSearch", g_rec.search == "дизайн", "search='" + g_rec.search + "'");
    g_rec.searchCleared = false;
    bool cleared = clickHit(HK_ACTION, -1, ACT_SEARCH_CLEAR);
    chk("крестик поиска → onSearchCleared", cleared && g_rec.searchCleared);
    SetWindowTextW(se, L"ещё");
    g_rec.searchCleared = false;
    sendKey(se, VK_ESCAPE);
    chk("Esc в поиске очищает", g_rec.searchCleared && textOf(se).empty());
  }

  // ---- 6. сообщества ----
  g_st.section = SEC_COMMUNITIES;
  ui::setState(g_st);
  repaint();
  g_rec.commId = 0;
  bool joinClicked = clickHit(HK_COMMUNITY_JOIN);
  chk("кнопка «Вступить/Покинуть» → onToggleCommunity", joinClicked && g_rec.commId != 0,
      "id=" + std::to_string(g_rec.commId));
  g_rec.commChat = 0;
  bool openClicked = clickHit(HK_COMMUNITY_CHAT);
  chk("кнопка «Открыть чат» → onOpenCommunityChat", openClicked && g_rec.commChat != 0,
      "chat=" + std::to_string(g_rec.commChat));
  g_rec.commId = 0;
  chk("клик по карточке сообщества → onToggleCommunity",
      clickHit(HK_COMMUNITY_CARD) && g_rec.commId != 0);

  // ---- 7. истории ----
  g_st.section = SEC_STORIES;
  ui::setState(g_st);
  repaint();
  g_rec.viewed = 0;
  chk("клик по карточке истории → onViewStory", clickHit(HK_STORY) && g_rec.viewed != 0,
      "story=" + std::to_string(g_rec.viewed));
  g_rec.storyAdd = false;
  chk("кнопка «История» → onCreateStory", clickHit(HK_ACTION, -1, ACT_STORY_ADD) && g_rec.storyAdd);

  // ---- 8. горячие клавиши и прокрутка ----
  for (int i = 0; i < 5; i++) {
    g_rec.section = -1;
    setCtrl(true);
    SendMessageW(h, WM_KEYDOWN, (WPARAM)('1' + i), 0);
    setCtrl(false);
    pump();
    int want[5] = {SEC_CHATS, SEC_FEED, SEC_COMMUNITIES, SEC_STORIES, SEC_PROFILE};
    chk((std::string("Ctrl+") + std::to_string(i + 1) + " → раздел").c_str(),
        g_rec.section == want[i], "got=" + std::to_string(g_rec.section));
  }
  // Ctrl+1..5 из дочернего EDIT (через подкласс, DLGC_WANTALLKEYS)
  g_rec.section = -1;
  if (HWND e = GetDlgItem(h, 2002)) {
    setCtrl(true);
    SendMessageW(e, WM_KEYDOWN, '2', 0);
    setCtrl(false);
    pump();
  }
  chk("Ctrl+2 из поля ввода → раздел", g_rec.section == SEC_FEED,
      "got=" + std::to_string(g_rec.section));
  chk("EDIT возвращает DLGC_WANTALLKEYS",
      (SendMessageW(GetDlgItem(h, 2002), WM_GETDLGCODE, 0, 0) & DLGC_WANTALLKEYS) != 0);
  g_rec.refresh = false;
  SendMessageW(h, WM_KEYDOWN, VK_F5, 0);
  pump();
  chk("F5 → onRefresh", g_rec.refresh);

  g_st.section = SEC_FEED;
  ui::setState(g_st);
  repaint();
  int before = om::ui::g_scrollFeed;
  SendMessageW(h, WM_MOUSEWHEEL, MAKEWPARAM(0, (WPARAM)(-WHEEL_DELTA * 3)),
               MAKELPARAM(700, 400));
  repaint();
  chk("колесо мыши прокручивает ленту", om::ui::g_scrollFeed > before,
      "before=" + std::to_string(before) + " after=" + std::to_string(om::ui::g_scrollFeed));

  g_st.section = SEC_CHATS;
  g_st.activeChat = 1;
  ui::setState(g_st);
  repaint();
  int mbefore = om::ui::g_scrollMsgs;
  SendMessageW(h, WM_MOUSEWHEEL, MAKEWPARAM(0, (WPARAM)(-WHEEL_DELTA * 2)),
               MAKELPARAM(om::ui::g_hwnd ? 900 : 900, 300));
  repaint();
  chk("колесо мыши прокручивает сообщения", om::ui::g_scrollMsgs >= mbefore);
  ui::scrollChatToBottom();

  // ---- 9. WM_SIZE ----
  SetWindowPos(h, nullptr, 0, 0, 1100, 720, SWP_NOMOVE | SWP_NOZORDER);
  repaint();
  colors = 0; orange = 0;
  chk("перерисовка после WM_SIZE", shot(L"D:\\messenger\\desktop\\tests\\shot-resized.png", colors, orange) &&
      colors >= 20, "colors=" + std::to_string(colors));

  // ---- 10. пустое состояние ----
  UiState empty;
  empty.section = SEC_CHATS;
  empty.realtime = false;
  empty.loading = true;
  ui::setState(empty);
  for (int sec = SEC_CHATS; sec <= SEC_PROFILE; sec++) {
    g_st.section = sec;
    UiState e2 = empty;
    e2.section = sec;
    ui::setState(e2);
    repaint();
  }
  chk("пустое состояние: все разделы отрисованы без падения", true);

  g_st = empty;
  fillState(g_st);
  ui::setState(g_st);
  repaint();

  printf("\n== UI HARNESS: PASS=%d FAIL=%d ==\n", g_pass, g_fail);
  DestroyWindow(h);
  ui::destroyMainWindow();
  Gdiplus::GdiplusShutdown(token);
  return g_fail ? 1 : 0;
}
