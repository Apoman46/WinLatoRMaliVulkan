// KutuphaneGUI_SDL2.cpp
// SDL2 + SDL2_ttf ile mobil/masaustu kutuphane yonetim GUI'si.
// C++17
//
// Cxxdroid / Linux derleme ornegi:
// g++ KutuphaneGUI_SDL2.cpp -std=c++17 -O2 -lSDL2 -lSDL2_ttf -o kutuphane
//
// Android/Cxxdroid'da SDL2 ve SDL2_ttf paketlerinin kurulu olmasi gerekir.
// Font olarak Android'de /system/fonts/Roboto-Regular.ttf ve
// /system/fonts/Roboto-Bold.ttf denenir. Masaustunde de yaygin font yollarina bakilir.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#endif

// ----------------------------- AYARLAR -----------------------------

static const char* BOOK_FILE = "kitap_verileri.txt";
static const char* MEMBER_FILE = "uye_verileri.txt";
static const char* LOAN_FILE = "odunc_verileri.txt";
static const char* CONFIG_FILE = "kutuphane_ayarlari.txt";

static double DAILY_FINE = 2.50;           // Artik degistirilebilir (Ayarlar sayfasindan).
static const double DEFAULT_DAILY_FINE = 2.50;
static const int DEFAULT_LOAN_DAYS = 14;
static const int MIN_LOAN_DAYS = 1;
static const int MAX_LOAN_DAYS = 3650;      // ~10 yil - herhangi bir sayi girilebilir.
static const double MIN_DAILY_FINE = 0.0;
static const double MAX_DAILY_FINE = 10000.0;

// Kullanicinin yazdigi metni ondalikli sayiya cevirir; virgulu noktaya cevirir.
static bool parseDouble(std::string s, double& out) {
    for (auto& c : s) if (c == ',') c = '.';
    // Bosluklari temizle.
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c){ return std::isspace(c); }), s.end());
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

static std::string lowerASCII(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string cleanPipe(std::string s) {
    std::replace(s.begin(), s.end(), '|', '-');
    return s;
}

static std::string timeToDate(time_t t) {
    if (t == 0) return "-";
    std::tm* tmv = std::localtime(&t);
    if (!tmv) return "-";
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d.%m.%Y", tmv);
    return buf;
}

static std::string timeToDateTime(time_t t) {
    if (t == 0) return "-";
    std::tm* tmv = std::localtime(&t);
    if (!tmv) return "-";
    char buf[48];
    std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", tmv);
    return buf;
}

static std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string p;
    while (std::getline(ss, p, '|')) out.push_back(p);
    return out;
}

static std::string clip(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    if (n <= 3) return s.substr(0, n);
    return s.substr(0, n - 3) + "...";
}

// ----------------------------- MODEL -----------------------------

class Book {
    int id = 0;
    std::string title;
    std::string author;
    int year = 0;
    int pages = 0;
    bool available = true;
    int borrower = 0;

public:
    Book() = default;
    Book(int i, const std::string& t, const std::string& a, int y, int p,
         bool av = true, int b = 0)
        : id(i), title(t), author(a), year(y), pages(p), available(av), borrower(b) {}

    int getId() const { return id; }
    const std::string& getTitle() const { return title; }
    const std::string& getAuthor() const { return author; }
    int getYear() const { return year; }
    int getPages() const { return pages; }
    bool isAvailable() const { return available; }
    int getBorrower() const { return borrower; }

    void setAvailable(bool v) { available = v; }
    void setBorrower(int v) { borrower = v; }
    void setTitle(const std::string& v) { title = cleanPipe(v); }
    void setAuthor(const std::string& v) { author = cleanPipe(v); }

    std::string save() const {
        return std::to_string(id) + "|" + title + "|" + author + "|" +
               std::to_string(year) + "|" + std::to_string(pages) + "|" +
               std::to_string(available ? 1 : 0) + "|" + std::to_string(borrower);
    }
};

class Member {
    int id = 0;
    std::string name;
    std::string phone;

public:
    Member() = default;
    Member(int i, const std::string& n, const std::string& p)
        : id(i), name(n), phone(p) {}

    int getId() const { return id; }
    const std::string& getName() const { return name; }
    const std::string& getPhone() const { return phone; }

    void setName(const std::string& v) { name = cleanPipe(v); }
    void setPhone(const std::string& v) { phone = cleanPipe(v); }

    std::string save() const {
        return std::to_string(id) + "|" + name + "|" + phone;
    }
};

class Loan {
    int id = 0;
    int bookId = 0;
    int memberId = 0;
    time_t borrowDate = 0;
    time_t returnDate = 0;
    time_t dueDate = 0;

public:
    Loan() = default;

    Loan(int i, int b, int m, time_t when, int days)
        : id(i), bookId(b), memberId(m), borrowDate(when), returnDate(0) {
        dueDate = when + static_cast<time_t>(days) * 24 * 60 * 60;
    }

    Loan(int i, int b, int m, time_t when, time_t ret, time_t due)
        : id(i), bookId(b), memberId(m), borrowDate(when),
          returnDate(ret), dueDate(due) {}

    int getId() const { return id; }
    int getBookId() const { return bookId; }
    int getMemberId() const { return memberId; }
    time_t getBorrowDate() const { return borrowDate; }
    time_t getReturnDate() const { return returnDate; }
    time_t getDueDate() const { return dueDate; }

    bool returned() const { return returnDate != 0; }

    void setReturnDate(time_t t) { returnDate = t; }

    int lateDays() const {
        time_t compare = returned() ? returnDate : time(nullptr);
        if (compare <= dueDate) return 0;
        long long sec = static_cast<long long>(compare - dueDate);
        return static_cast<int>(std::ceil(static_cast<double>(sec) / 86400.0));
    }

    double fine() const {
        return lateDays() * DAILY_FINE;
    }

    std::string save() const {
        return std::to_string(id) + "|" + std::to_string(bookId) + "|" +
               std::to_string(memberId) + "|" + std::to_string(static_cast<long long>(borrowDate)) +
               "|" + std::to_string(static_cast<long long>(returnDate)) + "|" +
               std::to_string(static_cast<long long>(dueDate));
    }
};

// ----------------------------- LIBRARY -----------------------------

class Library {
    std::vector<Book> books;
    std::vector<Member> members;
    std::vector<Loan> loans;

    int nextBook = 1;
    int nextMember = 1;
    int nextLoan = 1;
    int loanDays = DEFAULT_LOAN_DAYS;

    Book* findBook(int id) {
        for (auto& b : books) if (b.getId() == id) return &b;
        return nullptr;
    }

    Member* findMember(int id) {
        for (auto& m : members) if (m.getId() == id) return &m;
        return nullptr;
    }

    Loan* activeLoan(int bookId) {
        for (auto& l : loans)
            if (l.getBookId() == bookId && !l.returned()) return &l;
        return nullptr;
    }

    void loadConfig() {
        std::ifstream f(CONFIG_FILE);
        if (!(f >> loanDays) || loanDays < MIN_LOAN_DAYS || loanDays > MAX_LOAN_DAYS)
            loanDays = DEFAULT_LOAN_DAYS;

        double fine = 0.0;
        if (f >> fine && fine >= MIN_DAILY_FINE && fine <= MAX_DAILY_FINE)
            DAILY_FINE = fine;
        else
            DAILY_FINE = DEFAULT_DAILY_FINE;
    }

    void loadBooks() {
        std::ifstream f(BOOK_FILE);
        std::string line;
        while (std::getline(f, line)) {
            auto p = splitPipe(line);
            if (p.size() != 7) continue;
            try {
                int id = std::stoi(p[0]);
                books.emplace_back(id, p[1], p[2], std::stoi(p[3]), std::stoi(p[4]),
                                   std::stoi(p[5]) == 1, std::stoi(p[6]));
                nextBook = std::max(nextBook, id + 1);
            } catch (...) {}
        }
    }

    void loadMembers() {
        std::ifstream f(MEMBER_FILE);
        std::string line;
        while (std::getline(f, line)) {
            auto p = splitPipe(line);
            if (p.size() != 3) continue;
            try {
                int id = std::stoi(p[0]);
                members.emplace_back(id, p[1], p[2]);
                nextMember = std::max(nextMember, id + 1);
            } catch (...) {}
        }
    }

    void loadLoans() {
        std::ifstream f(LOAN_FILE);
        std::string line;
        while (std::getline(f, line)) {
            auto p = splitPipe(line);
            if (p.size() != 6) continue;
            try {
                int id = std::stoi(p[0]);
                loans.emplace_back(id, std::stoi(p[1]), std::stoi(p[2]),
                                   static_cast<time_t>(std::stoll(p[3])),
                                   static_cast<time_t>(std::stoll(p[4])),
                                   static_cast<time_t>(std::stoll(p[5])));
                nextLoan = std::max(nextLoan, id + 1);
            } catch (...) {}
        }
    }

public:
    Library() {
        loadConfig();
        loadBooks();
        loadMembers();
        loadLoans();
    }

    ~Library() { save(); }

    void save() const {
        std::ofstream c(CONFIG_FILE);
        c << loanDays << "\n";
        c << std::fixed << std::setprecision(2) << DAILY_FINE << "\n";

        std::ofstream b(BOOK_FILE);
        for (const auto& x : books) b << x.save() << "\n";

        std::ofstream m(MEMBER_FILE);
        for (const auto& x : members) m << x.save() << "\n";

        std::ofstream l(LOAN_FILE);
        for (const auto& x : loans) l << x.save() << "\n";
    }

    int getLoanDays() const { return loanDays; }
    void setLoanDays(int d) {
        d = std::max(MIN_LOAN_DAYS, std::min(MAX_LOAN_DAYS, d));
        loanDays = d;
    }

    double getDailyFine() const { return DAILY_FINE; }
    void setDailyFine(double v) {
        v = std::max(MIN_DAILY_FINE, std::min(MAX_DAILY_FINE, v));
        DAILY_FINE = v;
    }

    size_t bookCount() const { return books.size(); }
    size_t memberCount() const { return members.size(); }

    size_t availableCount() const {
        return static_cast<size_t>(std::count_if(books.begin(), books.end(),
            [](const Book& b) { return b.isAvailable(); }));
    }

    size_t borrowedCount() const { return books.size() - availableCount(); }

    size_t activeLoanCount() const {
        return static_cast<size_t>(std::count_if(loans.begin(), loans.end(),
            [](const Loan& l) { return !l.returned(); }));
    }

    size_t overdueCount() const {
        return static_cast<size_t>(std::count_if(loans.begin(), loans.end(),
            [](const Loan& l) { return !l.returned() && l.lateDays() > 0; }));
    }

    double totalUnpaidFine() const {
        double total = 0;
        for (const auto& l : loans)
            if (!l.returned()) total += l.fine();
        return total;
    }

    const std::vector<Book>& getBooks() const { return books; }
    const std::vector<Member>& getMembers() const { return members; }
    const std::vector<Loan>& getLoans() const { return loans; }

    const Book* getBook(int id) const {
        for (const auto& b : books) if (b.getId() == id) return &b;
        return nullptr;
    }

    const Member* getMember(int id) const {
        for (const auto& m : members) if (m.getId() == id) return &m;
        return nullptr;
    }

    bool addBook(const std::string& title, const std::string& author, int year, int pages,
                 std::string& msg) {
        if (title.empty() || author.empty() || year < 1 || pages < 1) {
            msg = "Kitap bilgileri gecersiz.";
            return false;
        }
        books.emplace_back(nextBook++, cleanPipe(title), cleanPipe(author), year, pages);
        save();
        msg = "Kitap eklendi.";
        return true;
    }

    bool removeBook(int id, std::string& msg) {
        Book* b = findBook(id);
        if (!b) { msg = "Kitap bulunamadi."; return false; }
        if (!b->isAvailable()) { msg = "Oduncteki kitap silinemez."; return false; }

        books.erase(std::remove_if(books.begin(), books.end(),
            [id](const Book& x) { return x.getId() == id; }), books.end());
        save();
        msg = "Kitap silindi.";
        return true;
    }

    bool addMember(const std::string& name, const std::string& phone, std::string& msg) {
        if (name.empty()) { msg = "Ad soyad bos olamaz."; return false; }
        members.emplace_back(nextMember++, cleanPipe(name), cleanPipe(phone));
        save();
        msg = "Uye eklendi.";
        return true;
    }

    bool removeMember(int id, std::string& msg) {
        Member* m = findMember(id);
        if (!m) { msg = "Uye bulunamadi."; return false; }

        for (const auto& l : loans)
            if (!l.returned() && l.getMemberId() == id) {
                msg = "Aktif oduncu olan uye silinemez.";
                return false;
            }

        members.erase(std::remove_if(members.begin(), members.end(),
            [id](const Member& x) { return x.getId() == id; }), members.end());
        save();
        msg = "Uye silindi.";
        return true;
    }

    bool borrowBook(int bookId, int memberId, std::string& msg) {
        Book* b = findBook(bookId);
        Member* m = findMember(memberId);

        if (!b) { msg = "Kitap bulunamadi."; return false; }
        if (!m) { msg = "Uye bulunamadi."; return false; }
        if (!b->isAvailable()) { msg = "Kitap zaten oduncte."; return false; }

        b->setAvailable(false);
        b->setBorrower(memberId);
        loans.emplace_back(nextLoan++, bookId, memberId, time(nullptr), loanDays);
        save();

        msg = "Kitap odunc verildi. Son tarih: " + timeToDate(loans.back().getDueDate());
        return true;
    }

    bool returnBook(int bookId, std::string& msg) {
        Book* b = findBook(bookId);
        Loan* l = activeLoan(bookId);

        if (!b || !l) { msg = "Aktif odunc kaydi bulunamadi."; return false; }

        l->setReturnDate(time(nullptr));
        b->setAvailable(true);
        b->setBorrower(0);

        if (l->lateDays() > 0) {
            std::ostringstream oss;
            oss << "Iade alindi. Gecikme: " << l->lateDays()
                << " gun, ceza: " << std::fixed << std::setprecision(2)
                << l->fine() << " TL";
            msg = oss.str();
        } else {
            msg = "Kitap zamaninda iade alindi.";
        }

        save();
        return true;
    }

    bool editBook(int id, const std::string& title, const std::string& author, std::string& msg) {
        Book* b = findBook(id);
        if (!b) { msg = "Kitap bulunamadi."; return false; }
        if (title.empty() || author.empty()) { msg = "Baslik ve yazar bos olamaz."; return false; }
        b->setTitle(title);
        b->setAuthor(author);
        save();
        msg = "Kitap guncellendi.";
        return true;
    }

    bool editMember(int id, const std::string& name, const std::string& phone, std::string& msg) {
        Member* m = findMember(id);
        if (!m) { msg = "Uye bulunamadi."; return false; }
        if (name.empty()) { msg = "Ad soyad bos olamaz."; return false; }
        m->setName(name);
        m->setPhone(phone);
        save();
        msg = "Uye guncellendi.";
        return true;
    }
};

// ----------------------------- UI -----------------------------

struct Color {
    Uint8 r, g, b, a;
};

static const Color BG       { 18, 22, 30, 255 };
static const Color PANEL    { 27, 32, 43, 255 };
static const Color PANEL2   { 34, 40, 53, 255 };
static const Color TEXT     { 236, 240, 246, 255 };
static const Color MUTED    { 156, 166, 181, 255 };
static const Color ACCENT   { 72, 132, 255, 255 };
static const Color SUCCESS  { 55, 190, 125, 255 };
static const Color DANGER   { 235, 82, 92, 255 };
static const Color WARNING  { 245, 180, 65, 255 };
static const Color WHITE    { 255, 255, 255, 255 };
static const Color BLACK    { 0, 0, 0, 255 };

static SDL_Color sc(Color c) { return SDL_Color{c.r, c.g, c.b, c.a}; }

static void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

static void drawRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderDrawRect(r, &rc);
}

static bool inside(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// Bir kosenin ceyrek dairesini doldurur. quadrant: 0=sol-ust 1=sag-ust 2=sol-alt 3=sag-alt
static void fillCornerQuad(SDL_Renderer* r, int cx, int cy, int radius, Color c, int quadrant) {
    if (radius <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int dy = 0; dy <= radius; ++dy) {
        int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
        int y = (quadrant < 2) ? (cy - dy) : (cy + dy);
        int xStart = (quadrant == 0 || quadrant == 2) ? (cx - dx) : cx;
        int xEnd   = (quadrant == 0 || quadrant == 2) ? cx : (cx + dx);
        SDL_RenderDrawLine(r, xStart, y, xEnd, y);
    }
}

// Kosesi yuvarlatilmis dolu dikdortgen. Arayuzu yumusatmak icin panel/buton/kutularda kullanilir.
static void fillRoundedRect(SDL_Renderer* r, int x, int y, int w, int h, int radius, Color c) {
    if (w <= 0 || h <= 0) return;
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    if (radius <= 0) { fillRect(r, x, y, w, h, c); return; }

    fillRect(r, x + radius, y, w - 2 * radius, h, c);
    fillRect(r, x, y + radius, radius, h - 2 * radius, c);
    fillRect(r, x + w - radius, y + radius, radius, h - 2 * radius, c);

    fillCornerQuad(r, x + radius, y + radius, radius, c, 0);
    fillCornerQuad(r, x + w - radius, y + radius, radius, c, 1);
    fillCornerQuad(r, x + radius, y + h - radius, radius, c, 2);
    fillCornerQuad(r, x + w - radius, y + h - radius, radius, c, 3);
}

// Kosesi yuvarlatilmis kenarlik: disa renkli cerceve, ice ise arka plan rengiyle dolgu.
static void roundedBorder(SDL_Renderer* r, int x, int y, int w, int h, int radius,
                           Color borderColor, Color innerColor, int thickness = 2) {
    fillRoundedRect(r, x, y, w, h, radius, borderColor);
    if (w > 2 * thickness && h > 2 * thickness) {
        fillRoundedRect(r, x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness,
                         std::max(0, radius - thickness), innerColor);
    }
}

// Hafif golge birakan panel: arayuze derinlik katmak icin.
static void shadowPanel(SDL_Renderer* r, int x, int y, int w, int h, int radius, Color c) {
    fillRoundedRect(r, x + 3, y + 4, w, h, radius, Color{0, 0, 0, 70});
    fillRoundedRect(r, x, y, w, h, radius, c);
}

class Font {
    TTF_Font* normal = nullptr;
    TTF_Font* bold = nullptr;

    static TTF_Font* tryOpen(const std::vector<std::string>& paths, int size) {
        for (const auto& p : paths) {
            TTF_Font* f = TTF_OpenFont(p.c_str(), size);
            if (f) return f;
        }
        return nullptr;
    }

public:
    bool load(int size = 18) {
        std::vector<std::string> normalPaths = {
            "/system/fonts/Roboto-Regular.ttf",
            "/system/fonts/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
            "Roboto-Regular.ttf", "DejaVuSans.ttf"
        };
        std::vector<std::string> boldPaths = {
            "/system/fonts/Roboto-Bold.ttf",
            "/system/fonts/NotoSans-Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
            "Roboto-Bold.ttf", "DejaVuSans-Bold.ttf"
        };

        normal = tryOpen(normalPaths, size);
        bold = tryOpen(boldPaths, size);
        if (!bold) bold = normal;
        return normal != nullptr;
    }

    void close() {
        if (bold && bold != normal) TTF_CloseFont(bold);
        if (normal) TTF_CloseFont(normal);
        normal = bold = nullptr;
    }

    TTF_Font* get(bool isBold = false) { return isBold ? bold : normal; }
};

static void text(SDL_Renderer* r, Font& font, const std::string& s,
                 int x, int y, Color c, bool bold = false) {
    if (!font.get(bold) || s.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font.get(bold), s.c_str(), sc(c));
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void centeredText(SDL_Renderer* r, Font& font, const std::string& s,
                         int x, int y, int w, Color c, bool bold = false) {
    if (!font.get(bold)) return;
    int tw = 0, th = 0;
    TTF_SizeUTF8(font.get(bold), s.c_str(), &tw, &th);
    text(r, font, s, x + (w - tw) / 2, y, c, bold);
}

static void roundPanel(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
    shadowPanel(r, x, y, w, h, 14, c);
}

struct Button {
    int x, y, w, h;
    std::string label;
    Color color = PANEL2;
    Color hover = ACCENT;
    bool enabled = true;

    bool clicked(int mx, int my) const {
        return enabled && inside(mx, my, x, y, w, h);
    }

    void draw(SDL_Renderer* r, Font& f, int mx, int my) const {
        bool isHover = enabled && inside(mx, my, x, y, w, h);
        Color c = isHover ? hover : color;
        int radius = std::min(12, h / 2);
        if (isHover) {
            fillRoundedRect(r, x - 2, y - 2, w + 4, h + 4, radius + 2, Color{c.r, c.g, c.b, 90});
        }
        fillRoundedRect(r, x, y, w, h, radius, c);
        centeredText(r, f, label, x, y + (h - 20) / 2, w, enabled ? WHITE : MUTED, true);
    }
};

enum Page {
    DASHBOARD,
    BOOKS,
    MEMBERS,
    LOANS,
    SETTINGS
};

enum DialogType {
    NONE,
    ADD_BOOK,
    EDIT_BOOK,
    ADD_MEMBER,
    EDIT_MEMBER,
    DELETE_BOOK,
    DELETE_MEMBER,
    BORROW,
    RETURN_BOOK,
    SETTING,
    FINE_SETTING
};

struct InputBox {
    std::string label;
    std::string value;
    bool active = false;
    int maxLen = 120;
    bool numericOnly = false;   // true ise sadece rakam (ve gerekirse nokta/virgul) kabul edilir.
    bool allowDecimal = false;  // ondalikli sayilara (ucret gibi) izin verir.

    void draw(SDL_Renderer* r, Font& f, int x, int y, int w, int h) const {
        text(r, f, label, x, y - 24, MUTED, true);
        roundedBorder(r, x, y, w, h, 10, active ? ACCENT : Color{62, 70, 87, 255},
                      Color{22, 26, 35, 255}, active ? 2 : 1);
        text(r, f, value.empty() ? (active ? "" : "Dokunup yazin...") : value,
             x + 14, y + 10, value.empty() ? MUTED : TEXT, false);

        if (active) {
            int tw = 0, th = 0;
            if (f.get()) TTF_SizeUTF8(f.get(), value.c_str(), &tw, &th);
            // Yanip sonen imlec izlenimi icin yumusak bir cizgi.
            fillRect(r, x + 14 + tw + 2, y + 8, 2, h - 16, ACCENT);
        }
    }
};

class App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Font font;
    Library library;

    int width = 1100;
    int height = 720;
    bool running = true;
    bool textInput = false;

    Page page = DASHBOARD;
    DialogType dialog = NONE;

    std::string toast;
    Uint32 toastUntil = 0;
    bool toastGood = true;

    std::string search;
    int selectedBook = -1;
    int selectedMember = -1;

    int sortMode = 0;
    bool sortAsc = true;

    InputBox in1{"Baslik", "", false};
    InputBox in2{"Yazar", "", false};
    InputBox in3{"Yayin yili", "", false};
    InputBox in4{"Sayfa sayisi", "", false};

    int dialogId = -1;

    // Ana icerik alaninin genisligi: pencere boyutuna gore otomatik uyarlanir.
    // Boylece hem genis PC ekranlarinda hem de dar telefon ekranlarinda
    // duzen tasmadan/kirpilmadan rahatca kullanilabilir.
    int cw() const { return std::max(340, width - 245 - 20); }

    // 810 genislikte tasarlanan orijinal sutun konumlarini gercek icerik
    // genisligine oranla olcekler (tum tablo/kart konumlari icin kullanilir).
    int sc(int originalOffset) const { return (originalOffset * cw()) / 810; }

    void showToast(const std::string& s, bool good = true) {
        toast = s;
        toastGood = good;
        toastUntil = SDL_GetTicks() + 3500;
    }

    void setDialog(DialogType d, int id = -1) {
        dialog = d;
        dialogId = id;
        in1.value = "";
        in2.value = "";
        in3.value = "";
        in4.value = "";
        in1.active = in2.active = in3.active = in4.active = false;

        // Her diyalog acilisinda hangi alanlarin sadece sayisal oldugunu belirle.
        in1.numericOnly = in2.numericOnly = in3.numericOnly = in4.numericOnly = false;
        in1.allowDecimal = in2.allowDecimal = in3.allowDecimal = in4.allowDecimal = false;

        if (d == ADD_BOOK) {
            in3.numericOnly = true; // Yayin yili
            in4.numericOnly = true; // Sayfa sayisi
        }
        if (d == BORROW || d == RETURN_BOOK) {
            in1.numericOnly = true; // Kitap ID
            in2.numericOnly = true; // Uye ID
        }
        if (d == SETTING) {
            in1.numericOnly = true; // Gun sayisi
        }
        if (d == FINE_SETTING) {
            in1.numericOnly = true;
            in1.allowDecimal = true; // Ucret ondalikli olabilir (ornek: 2.50)
        }

        if (d == EDIT_BOOK && id >= 0) {
            const Book* b = library.getBook(id);
            if (b) {
                in1.value = b->getTitle();
                in2.value = b->getAuthor();
            }
        }

        if (d == EDIT_MEMBER && id >= 0) {
            const Member* m = library.getMember(id);
            if (m) {
                in1.value = m->getName();
                in2.value = m->getPhone();
            }
        }

        if (d == SETTING) {
            in1.value = std::to_string(library.getLoanDays());
        }

        if (d == FINE_SETTING) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << library.getDailyFine();
            in1.value = oss.str();
        }

        textInput = (d != NONE);
        if (textInput) SDL_StartTextInput();
        else SDL_StopTextInput();
    }

    void closeDialog() {
        dialog = NONE;
        dialogId = -1;
        textInput = false;
        SDL_StopTextInput();
    }

    void activateInput(int idx) {
        in1.active = in2.active = in3.active = in4.active = false;
        if (idx == 1) in1.active = true;
        if (idx == 2) in2.active = true;
        if (idx == 3) in3.active = true;
        if (idx == 4) in4.active = true;

        // Bir alana her tiklandiginda klavyeyi kesin olarak ac (yil, sayfa gibi
        // sayisal alanlar dahil) - telefonlarda klavye bazen kapanabiliyor.
        SDL_StartTextInput();
        textInput = true;
    }

    InputBox* activeInput() {
        if (in1.active) return &in1;
        if (in2.active) return &in2;
        if (in3.active) return &in3;
        if (in4.active) return &in4;
        return nullptr;
    }

    void handleText(const char* txt) {
        InputBox* box = activeInput();
        if (!box || !txt) return;
        std::string add = txt;

        if (box->numericOnly) {
            std::string filtered;
            for (char c : add) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    filtered += c;
                } else if (box->allowDecimal && (c == '.' || c == ',') &&
                           box->value.find('.') == std::string::npos) {
                    filtered += '.';
                }
            }
            add = filtered;
        }

        if (add.empty()) return;
        if (box->value.size() + add.size() <= static_cast<size_t>(box->maxLen))
            box->value += add;
    }

    void handleBackspace() {
        InputBox* box = activeInput();
        if (!box || box->value.empty()) return;
        size_t n = box->value.size();

        // UTF-8 karakterini tek tek silebilmek icin geriye git.
        while (n > 0) {
            --n;
            unsigned char c = static_cast<unsigned char>(box->value[n]);
            if ((c & 0xC0) != 0x80) {
                box->value.erase(n);
                break;
            }
        }
    }

    void renderHeader() {
        fillRect(renderer, 0, 0, width, 72, PANEL);
        text(renderer, font, "KUTUPHANE", 24, 17, TEXT, true);
        text(renderer, font, "SDL2 Yonetim Sistemi", 160, 20, MUTED);

        Button saveBtn{width - 190, 14, 78, 42, "KAYDET", SUCCESS, SUCCESS};
        Button exitBtn{width - 102, 14, 78, 42, "CIKIS", DANGER, DANGER};
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        saveBtn.draw(renderer, font, mx, my);
        exitBtn.draw(renderer, font, mx, my);
    }

    void renderSidebar() {
        fillRect(renderer, 0, 72, 220, height - 72, Color{21, 25, 34, 255});

        const char* labels[] = {
            "Ana Sayfa", "Kitaplar", "Uyeler", "Odunc / Iade", "Ayarlar"
        };

        Page pages[] = {DASHBOARD, BOOKS, MEMBERS, LOANS, SETTINGS};

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        for (int i = 0; i < 5; ++i) {
            int y = 96 + i * 58;
            bool sel = page == pages[i];
            bool hov = inside(mx, my, 12, y, 196, 46);
            Color c = sel ? ACCENT : (hov ? Color{30,36,48,255} : Color{21,25,34,255});
            fillRoundedRect(renderer, 12, y, 196, 46, 10, c);
            text(renderer, font, labels[i], 30, y + 12,
                 sel ? WHITE : TEXT, sel);
        }

        fillRect(renderer, 12, height - 118, 196, 1, Color{54,62,76,255});
        text(renderer, font, "Odunc suresi", 26, height - 102, MUTED);
        text(renderer, font, std::to_string(library.getLoanDays()) + " gun",
             26, height - 76, TEXT, true);

        std::ostringstream fineOss;
        fineOss << std::fixed << std::setprecision(2) << library.getDailyFine() << " TL/gun";
        text(renderer, font, "Gecikme ucreti", 26, height - 48, MUTED);
        text(renderer, font, fineOss.str(), 26, height - 22, WARNING, true);
    }

    void renderDashboard() {
        int x = 245;
        text(renderer, font, "Genel Bakis", x, 102, TEXT, true);
        text(renderer, font, "Kutuphane durumunu tek ekranda gor.", x, 132, MUTED);

        struct Card { std::string title; std::string value; Color c; };
        Card cards[] = {
            {"Toplam Kitap", std::to_string(library.bookCount()), ACCENT},
            {"Mevcut Kitap", std::to_string(library.availableCount()), SUCCESS},
            {"Oduncteki", std::to_string(library.borrowedCount()), WARNING},
            {"Uye", std::to_string(library.memberCount()), Color{160,110,240,255}},
            {"Geciken", std::to_string(library.overdueCount()), DANGER}
        };

        int cardW = std::max(110, (cw() - 4 * 12) / 5);
        for (int i = 0; i < 5; ++i) {
            int cx = x + i * (cardW + 12);
            shadowPanel(renderer, cx, 178, cardW, 112, 12, PANEL);
            fillRoundedRect(renderer, cx, 178, 6, 112, 3, cards[i].c);
            text(renderer, font, clip(cards[i].title, 16), cx + 18, 196, MUTED);
            text(renderer, font, cards[i].value, cx + 18, 230, TEXT, true);
        }

        roundPanel(renderer, x, 315, cw(), 330, PANEL);
        text(renderer, font, "Hizli Islemler", x + 20, 335, TEXT, true);

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        Button b1{x + 20, 380, sc(180), 54, "KITAP EKLE", ACCENT, ACCENT};
        Button b2{x + sc(220), 380, sc(180), 54, "UYE EKLE", SUCCESS, SUCCESS};
        Button b3{x + sc(420), 380, sc(180), 54, "ODUNC VER", PANEL2, ACCENT};
        Button b4{x + sc(620), 380, sc(165), 54, "IADE AL", PANEL2, SUCCESS};

        b1.draw(renderer, font, mx, my);
        b2.draw(renderer, font, mx, my);
        b3.draw(renderer, font, mx, my);
        b4.draw(renderer, font, mx, my);

        text(renderer, font, "Aktif odunc", x + 20, 470, MUTED);
        text(renderer, font, std::to_string(library.activeLoanCount()) + " kayit",
             x + 20, 500, TEXT, true);

        text(renderer, font, "Tahmini aktif gecikme", x + sc(260), 470, MUTED);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << library.totalUnpaidFine() << " TL";
        text(renderer, font, oss.str(), x + sc(260), 500, TEXT, true);

        text(renderer, font, "Bugun: " + timeToDate(time(nullptr)),
             x + 20, 580, MUTED);

        text(renderer, font, "Veriler her islemden sonra otomatik kaydedilir.",
             x + 20, 612, MUTED);
    }

    void renderSearchBar(int x, int y, int w) {
        bool active = in1.active && dialog == NONE;
        roundedBorder(renderer, x, y, w, 46, 10, active ? ACCENT : Color{60,68,84,255},
                      Color{22,26,35,255}, active ? 2 : 1);
        text(renderer, font, search.empty() ? "Ara..." : search, x + 16, y + 13,
             search.empty() ? MUTED : TEXT);
    }

    void renderBooks() {
        int x = 245;
        text(renderer, font, "Kitaplar", x, 100, TEXT, true);
        text(renderer, font, "Ekle, ara, duzenle veya sil.", x, 130, MUTED);

        renderSearchBar(x, 165, sc(440));

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        Button add{x + sc(455), 165, sc(130), 46, "+ KITAP", ACCENT, ACCENT};
        add.draw(renderer, font, mx, my);

        Button sort{x + sc(600), 165, sc(160), 46, sortAsc ? "SIRALA A-Z" : "SIRALA Z-A",
                    PANEL2, ACCENT};
        sort.draw(renderer, font, mx, my);

        std::vector<const Book*> list;
        std::string q = lowerASCII(search);

        for (const auto& b : library.getBooks()) {
            if (q.empty() ||
                lowerASCII(b.getTitle()).find(q) != std::string::npos ||
                lowerASCII(b.getAuthor()).find(q) != std::string::npos) {
                list.push_back(&b);
            }
        }

        std::sort(list.begin(), list.end(), [this](const Book* a, const Book* b) {
            int cmp = 0;
            if (sortMode == 1) {
                if (a->getAuthor() < b->getAuthor()) cmp = -1;
                else if (a->getAuthor() > b->getAuthor()) cmp = 1;
            } else {
                if (a->getTitle() < b->getTitle()) cmp = -1;
                else if (a->getTitle() > b->getTitle()) cmp = 1;
            }
            return sortAsc ? (cmp < 0) : (cmp > 0);
        });

        int top = 235;
        fillRect(renderer, x, top, cw(), 38, Color{39,46,60,255});
        text(renderer, font, "ID", x + 12, top + 9, MUTED, true);
        text(renderer, font, "Baslik", x + sc(65), top + 9, MUTED, true);
        text(renderer, font, "Yazar", x + sc(355), top + 9, MUTED, true);
        text(renderer, font, "Yil", x + sc(555), top + 9, MUTED, true);
        text(renderer, font, "Durum", x + sc(625), top + 9, MUTED, true);

        int y = top + 40;
        int shown = 0;

        for (const Book* b : list) {
            if (shown >= 8) break;
            bool sel = selectedBook == b->getId();

            fillRect(renderer, x, y, cw(), 45, sel ? Color{43,72,116,255} :
                     (shown % 2 ? PANEL : Color{31,37,49,255}));

            text(renderer, font, std::to_string(b->getId()), x + 12, y + 12, TEXT);
            text(renderer, font, clip(b->getTitle(), 32), x + sc(65), y + 12, TEXT);
            text(renderer, font, clip(b->getAuthor(), 22), x + sc(355), y + 12, TEXT);
            text(renderer, font, std::to_string(b->getYear()), x + sc(555), y + 12, TEXT);

            Color dc = b->isAvailable() ? SUCCESS : WARNING;
            text(renderer, font, b->isAvailable() ? "MEVCUT" : "ODUNCTE",
                 x + sc(625), y + 12, dc, true);

            y += 46;
            shown++;
        }

        if (list.empty())
            text(renderer, font, "Sonuc bulunamadi.", x + 20, y + 20, MUTED);

        if (selectedBook >= 0) {
            fillRoundedRect(renderer, x, 620, cw(), 48, 10, PANEL2);
            text(renderer, font, "Secili ID: " + std::to_string(selectedBook),
                 x + 14, 635, TEXT, true);

            Button edit{x + sc(230), 622, sc(120), 42, "DUZENLE", PANEL2, ACCENT};
            Button del{x + sc(360), 622, sc(120), 42, "SIL", DANGER, DANGER};
            edit.draw(renderer, font, mx, my);
            del.draw(renderer, font, mx, my);
        }
    }

    void renderMembers() {
        int x = 245;
        text(renderer, font, "Uyeler", x, 100, TEXT, true);
        text(renderer, font, "Uye kayitlarini yonet.", x, 130, MUTED);

        renderSearchBar(x, 165, sc(440));

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        Button add{x + sc(455), 165, sc(130), 46, "+ UYE", SUCCESS, SUCCESS};
        add.draw(renderer, font, mx, my);

        int top = 235;
        fillRect(renderer, x, top, cw(), 38, Color{39,46,60,255});
        text(renderer, font, "ID", x + 15, top + 9, MUTED, true);
        text(renderer, font, "Ad Soyad", x + sc(80), top + 9, MUTED, true);
        text(renderer, font, "Telefon", x + sc(430), top + 9, MUTED, true);

        std::string q = lowerASCII(search);
        int y = top + 40;
        int shown = 0;

        for (const auto& m : library.getMembers()) {
            if (!q.empty() &&
                lowerASCII(m.getName()).find(q) == std::string::npos &&
                lowerASCII(m.getPhone()).find(q) == std::string::npos)
                continue;

            if (shown >= 8) break;

            bool sel = selectedMember == m.getId();
            fillRect(renderer, x, y, cw(), 45, sel ? Color{43,72,116,255} :
                     (shown % 2 ? PANEL : Color{31,37,49,255}));

            text(renderer, font, std::to_string(m.getId()), x + 15, y + 12, TEXT);
            text(renderer, font, clip(m.getName(), 38), x + sc(80), y + 12, TEXT);
            text(renderer, font, clip(m.getPhone(), 28), x + sc(430), y + 12, TEXT);

            y += 46;
            shown++;
        }

        if (shown == 0)
            text(renderer, font, "Uye bulunamadi.", x + 20, y + 20, MUTED);

        if (selectedMember >= 0) {
            fillRoundedRect(renderer, x, 620, cw(), 48, 10, PANEL2);
            text(renderer, font, "Secili ID: " + std::to_string(selectedMember),
                 x + 14, 635, TEXT, true);

            Button edit{x + sc(230), 622, sc(120), 42, "DUZENLE", PANEL2, ACCENT};
            Button del{x + sc(360), 622, sc(120), 42, "SIL", DANGER, DANGER};
            edit.draw(renderer, font, mx, my);
            del.draw(renderer, font, mx, my);
        }
    }

    void renderLoans() {
        int x = 245;
        text(renderer, font, "Odunc / Iade", x, 100, TEXT, true);
        text(renderer, font, "Aktif odunclari ve gecikmeleri takip et.", x, 130, MUTED);

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        Button borrow{x, 165, sc(160), 46, "ODUNC VER", ACCENT, ACCENT};
        Button ret{x + sc(175), 165, sc(160), 46, "IADE AL", SUCCESS, SUCCESS};
        borrow.draw(renderer, font, mx, my);
        ret.draw(renderer, font, mx, my);

        int top = 235;
        fillRect(renderer, x, top, cw(), 38, Color{39,46,60,255});
        text(renderer, font, "Kitap", x + 15, top + 9, MUTED, true);
        text(renderer, font, "Uye", x + sc(270), top + 9, MUTED, true);
        text(renderer, font, "Alis", x + sc(475), top + 9, MUTED, true);
        text(renderer, font, "Son Tarih", x + sc(580), top + 9, MUTED, true);
        text(renderer, font, "Durum", x + sc(700), top + 9, MUTED, true);

        int y = top + 40;
        int shown = 0;

        for (const auto& l : library.getLoans()) {
            if (l.returned()) continue;
            if (shown >= 7) break;

            const Book* b = library.getBook(l.getBookId());
            const Member* m = library.getMember(l.getMemberId());

            fillRect(renderer, x, y, cw(), 48, shown % 2 ? PANEL : Color{31,37,49,255});

            text(renderer, font, b ? clip(b->getTitle(), 27) : "Bilinmiyor",
                 x + 15, y + 13, TEXT);
            text(renderer, font, m ? clip(m->getName(), 21) : "Bilinmiyor",
                 x + sc(270), y + 13, TEXT);
            text(renderer, font, timeToDate(l.getBorrowDate()),
                 x + sc(475), y + 13, TEXT);
            text(renderer, font, timeToDate(l.getDueDate()),
                 x + sc(580), y + 13, TEXT);

            if (l.lateDays() > 0) {
                text(renderer, font, "GECIKTI", x + sc(700), y + 13, DANGER, true);
            } else {
                text(renderer, font, "AKTIF", x + sc(700), y + 13, SUCCESS, true);
            }

            y += 49;
            shown++;
        }

        if (shown == 0)
            text(renderer, font, "Aktif odunc kaydi yok.", x + 15, y + 20, MUTED);
    }

    void renderSettings() {
        int x = 245;
        text(renderer, font, "Ayarlar", x, 100, TEXT, true);
        text(renderer, font, "Sistem tercihlerini buradan degistir.", x, 130, MUTED);

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        // --- Odunc suresi karti ---
        roundPanel(renderer, x, 180, cw(), 195, PANEL);
        text(renderer, font, "Odunc suresi", x + 24, 205, TEXT, true);
        text(renderer, font, "Yeni odunc kayitlarinin son teslim suresi. Istediginiz herhangi bir gun sayisini girebilirsiniz.",
             x + 24, 235, MUTED);

        roundedBorder(renderer, x + 24, 275, 220, 55, 10, Color{60,68,84,255}, Color{22,26,35,255});
        centeredText(renderer, font, std::to_string(library.getLoanDays()) + " gun",
                     x + 24, 291, 220, TEXT, true);

        Button minus1a{x + 260, 275, 50, 55, "-1", PANEL2, ACCENT};
        Button plus1a{x + 316, 275, 50, 55, "+1", PANEL2, ACCENT};
        Button minus10a{x + 372, 275, 55, 55, "-10", PANEL2, ACCENT};
        Button plus10a{x + 433, 275, 55, 55, "+10", PANEL2, ACCENT};
        Button changeDays{x + 500, 275, 150, 55, "TAM DEGER GIR", ACCENT, ACCENT};
        minus1a.draw(renderer, font, mx, my);
        plus1a.draw(renderer, font, mx, my);
        minus10a.draw(renderer, font, mx, my);
        plus10a.draw(renderer, font, mx, my);
        changeDays.draw(renderer, font, mx, my);

        // --- Gecikme ucreti karti ---
        roundPanel(renderer, x, 395, cw(), 195, PANEL);
        text(renderer, font, "Gecikme ucreti (gunluk)", x + 24, 420, TEXT, true);
        text(renderer, font, "Zamaninda iade edilmeyen kitaplar icin gunluk ceza tutari.",
             x + 24, 450, MUTED);

        roundedBorder(renderer, x + 24, 490, 220, 55, 10, Color{60,68,84,255}, Color{22,26,35,255});
        std::ostringstream fineStr;
        fineStr << std::fixed << std::setprecision(2) << library.getDailyFine() << " TL";
        centeredText(renderer, font, fineStr.str(), x + 24, 506, 220, WARNING, true);

        Button minus1b{x + 260, 490, 65, 55, "-1 TL", PANEL2, ACCENT};
        Button plus1b{x + 331, 490, 65, 55, "+1 TL", PANEL2, ACCENT};
        Button changeFine{x + 500, 490, 150, 55, "TAM DEGER GIR", ACCENT, ACCENT};
        minus1b.draw(renderer, font, mx, my);
        plus1b.draw(renderer, font, mx, my);
        changeFine.draw(renderer, font, mx, my);

        text(renderer, font, "Veriler her islemden sonra otomatik olarak kaydedilir.",
             x + 24, 615, MUTED);
    }

    // Dialog kutusunun boyutu/konumu: kucuk ekranlarda (telefon) tasmayacak
    // sekilde otomatik kuculur. renderDialog, clickDialog ve Enter-tusu
    // isleyicisi hep bu tek fonksiyonu kullanir, boylece hicbir zaman
    // birbirinden sapmaz.
    void dialogGeom(int& dw, int& dh, int& dx, int& dy) const {
        // Genislik dar ekranlarda kucultulur (yatay tasmayi onler).
        dw = std::max(320, std::min(600, width - 40));
        // Yukseklik: ic duzen (etiket + 4 alan + butonlar) sabit 400px'e gore
        // tasarlandigi icin sadece cok kisa pencerelerde kucultulur.
        dh = std::max(340, std::min(400, height - 40));
        dx = (width - dw) / 2;
        dy = (height - dh) / 2;
    }

    void renderDialog() {
        if (dialog == NONE) return;

        fillRect(renderer, 0, 0, width, height, Color{0,0,0,150});

        int dw, dh, dx, dy;
        dialogGeom(dw, dh, dx, dy);

        shadowPanel(renderer, dx, dy, dw, dh, 16, PANEL);
        // ince ust vurgu cizgisi
        fillRoundedRect(renderer, dx, dy, dw, 4, 2, ACCENT);

        std::string title = "Islem";
        if (dialog == ADD_BOOK) title = "Yeni Kitap";
        if (dialog == EDIT_BOOK) title = "Kitabi Duzenle";
        if (dialog == ADD_MEMBER) title = "Yeni Uye";
        if (dialog == EDIT_MEMBER) title = "Uyeyi Duzenle";
        if (dialog == DELETE_BOOK) title = "Kitap Sil";
        if (dialog == DELETE_MEMBER) title = "Uye Sil";
        if (dialog == BORROW) title = "Kitap Odunc Ver";
        if (dialog == RETURN_BOOK) title = "Kitap Iade Al";
        if (dialog == SETTING) title = "Odunc Suresini Degistir";
        if (dialog == FINE_SETTING) title = "Gecikme Ucretini Degistir";

        text(renderer, font, title, dx + 24, dy + 22, TEXT, true);

        int mx, my;
        SDL_GetMouseState(&mx, &my);

        if (dialog == ADD_BOOK || dialog == EDIT_BOOK) {
            in1.label = "Kitap basligi";
            in2.label = "Yazar";
            in1.draw(renderer, font, dx + 30, dy + 90, 540, 48);
            in2.draw(renderer, font, dx + 30, dy + 170, 540, 48);

            if (dialog == ADD_BOOK) {
                in3.label = "Yayin yili";
                in4.label = "Sayfa sayisi";
                in3.draw(renderer, font, dx + 30, dy + 250, 250, 48);
                in4.draw(renderer, font, dx + 320, dy + 250, 250, 48);
            }

            Button ok{dx + 350, dy + 330, 105, 46, "KAYDET", SUCCESS, SUCCESS};
            Button cancel{dx + 465, dy + 330, 105, 46, "IPTAL", DANGER, DANGER};
            ok.draw(renderer, font, mx, my);
            cancel.draw(renderer, font, mx, my);
            return;
        }

        if (dialog == ADD_MEMBER || dialog == EDIT_MEMBER) {
            in1.label = "Ad soyad";
            in2.label = "Telefon";
            in1.draw(renderer, font, dx + 30, dy + 100, 540, 48);
            in2.draw(renderer, font, dx + 30, dy + 190, 540, 48);

            Button ok{dx + 350, dy + 330, 105, 46, "KAYDET", SUCCESS, SUCCESS};
            Button cancel{dx + 465, dy + 330, 105, 46, "IPTAL", DANGER, DANGER};
            ok.draw(renderer, font, mx, my);
            cancel.draw(renderer, font, mx, my);
            return;
        }

        if (dialog == BORROW || dialog == RETURN_BOOK || dialog == SETTING || dialog == FINE_SETTING) {
            if (dialog == SETTING) in1.label = "Gun sayisi (istediginiz herhangi bir sayi)";
            else if (dialog == FINE_SETTING) in1.label = "Gunluk ucret (TL, ornek: 2.50)";
            else in1.label = "Kitap ID";
            in2.label = dialog == BORROW ? "Uye ID" : "";

            if (dialog == SETTING || dialog == FINE_SETTING) {
                in1.draw(renderer, font, dx + 30, dy + 110, 540, 48);
            } else {
                in1.draw(renderer, font, dx + 30, dy + 110, 540, 48);
                if (dialog == BORROW)
                    in2.draw(renderer, font, dx + 30, dy + 195, 540, 48);
            }

            Button ok{dx + 350, dy + 330, 105, 46, "ONAYLA", SUCCESS, SUCCESS};
            Button cancel{dx + 465, dy + 330, 105, 46, "IPTAL", DANGER, DANGER};
            ok.draw(renderer, font, mx, my);
            cancel.draw(renderer, font, mx, my);

            if (dialog == BORROW)
                text(renderer, font, "Mevcut odunc suresi: " +
                     std::to_string(library.getLoanDays()) + " gun",
                     dx + 30, dy + 285, MUTED);
            if (dialog == SETTING)
                text(renderer, font, "Izin verilen aralik: " + std::to_string(MIN_LOAN_DAYS) +
                     " - " + std::to_string(MAX_LOAN_DAYS) + " gun", dx + 30, dy + 285, MUTED);
            if (dialog == FINE_SETTING)
                text(renderer, font, "Izin verilen aralik: 0 - " +
                     std::to_string(static_cast<int>(MAX_DAILY_FINE)) + " TL", dx + 30, dy + 285, MUTED);
            return;
        }

        if (dialog == DELETE_BOOK || dialog == DELETE_MEMBER) {
            std::string what = dialog == DELETE_BOOK ? "kitabi" : "uyeyi";
            text(renderer, font, "Secili " + what + " silmek istediginize emin misiniz?",
                 dx + 30, dy + 105, TEXT);
            text(renderer, font, "ID: " + std::to_string(dialogId),
                 dx + 30, dy + 145, WARNING, true);

            Button ok{dx + 350, dy + 300, 105, 46, "SIL", DANGER, DANGER};
            Button cancel{dx + 465, dy + 300, 105, 46, "IPTAL", PANEL2, ACCENT};
            ok.draw(renderer, font, mx, my);
            cancel.draw(renderer, font, mx, my);
        }
    }

    void renderToast() {
        if (toast.empty() || SDL_GetTicks() > toastUntil) return;
        int w = std::min(700, static_cast<int>(toast.size()) * 10 + 50);
        int x = (width - w) / 2;
        int y = height - 64;

        shadowPanel(renderer, x, y, w, 42, 10, toastGood ? Color{27,90,62,255} : Color{110,39,47,255});
        text(renderer, font, toast, x + 18, y + 11, WHITE, true);
    }

    void render() {
        SDL_SetRenderDrawColor(renderer, BG.r, BG.g, BG.b, BG.a);
        SDL_RenderClear(renderer);

        renderHeader();
        renderSidebar();

        if (page == DASHBOARD) renderDashboard();
        else if (page == BOOKS) renderBooks();
        else if (page == MEMBERS) renderMembers();
        else if (page == LOANS) renderLoans();
        else if (page == SETTINGS) renderSettings();

        renderDialog();
        renderToast();

        SDL_RenderPresent(renderer);
    }

    void clickNormal(int mx, int my) {
        // Header
        if (inside(mx, my, width - 190, 14, 78, 42)) {
            library.save();
            showToast("Veriler kaydedildi.", true);
            return;
        }
        if (inside(mx, my, width - 102, 14, 78, 42)) {
            running = false;
            return;
        }

        // Sidebar
        if (mx < 220 && my >= 96 && my <= 386) {
            int idx = (my - 96) / 58;
            if (idx >= 0 && idx < 5) {
                page = static_cast<Page>(idx);
                search.clear();
                selectedBook = selectedMember = -1;
                return;
            }
        }

        if (page == DASHBOARD) {
            int x = 245;
            if (inside(mx, my, x + 20, 380, sc(180), 54)) {
                setDialog(ADD_BOOK);
                return;
            }
            if (inside(mx, my, x + sc(220), 380, sc(180), 54)) {
                setDialog(ADD_MEMBER);
                return;
            }
            if (inside(mx, my, x + sc(420), 380, sc(180), 54)) {
                setDialog(BORROW);
                return;
            }
            if (inside(mx, my, x + sc(620), 380, sc(165), 54)) {
                setDialog(RETURN_BOOK);
                return;
            }
        }

        if (page == BOOKS) {
            int x = 245;
            if (inside(mx, my, x, 165, sc(440), 46)) {
                activateInput(1); // Search is handled specially below.
                in1.value = search;
                return;
            }
            if (inside(mx, my, x + sc(455), 165, sc(130), 46)) {
                setDialog(ADD_BOOK);
                return;
            }
            if (inside(mx, my, x + sc(600), 165, sc(160), 46)) {
                sortAsc = !sortAsc;
                return;
            }

            int top = 275;
            int row = (my - top) / 46;
            if (mx >= x && mx <= x + cw() && row >= 0 && row < 8) {
                std::vector<const Book*> list;
                std::string q = lowerASCII(search);
                for (const auto& b : library.getBooks())
                    if (q.empty() || lowerASCII(b.getTitle()).find(q) != std::string::npos ||
                        lowerASCII(b.getAuthor()).find(q) != std::string::npos)
                        list.push_back(&b);

                std::sort(list.begin(), list.end(), [this](const Book* a, const Book* b) {
                    bool z = sortMode == 1 ? a->getAuthor() < b->getAuthor()
                                           : a->getTitle() < b->getTitle();
                    return sortAsc ? z : !z;
                });

                if (row < static_cast<int>(list.size()))
                    selectedBook = list[row]->getId();
                return;
            }

            if (selectedBook >= 0) {
                if (inside(mx, my, x + sc(230), 622, sc(120), 42)) {
                    setDialog(EDIT_BOOK, selectedBook);
                    return;
                }
                if (inside(mx, my, x + sc(360), 622, sc(120), 42)) {
                    setDialog(DELETE_BOOK, selectedBook);
                    return;
                }
            }
        }

        if (page == MEMBERS) {
            int x = 245;
            if (inside(mx, my, x, 165, sc(440), 46)) {
                in1.value = search;
                activateInput(1);
                return;
            }
            if (inside(mx, my, x + sc(455), 165, sc(130), 46)) {
                setDialog(ADD_MEMBER);
                return;
            }

            int top = 275;
            int row = (my - top) / 46;
            if (mx >= x && mx <= x + cw() && row >= 0 && row < 8) {
                std::string q = lowerASCII(search);
                std::vector<const Member*> list;
                for (const auto& m : library.getMembers())
                    if (q.empty() || lowerASCII(m.getName()).find(q) != std::string::npos ||
                        lowerASCII(m.getPhone()).find(q) != std::string::npos)
                        list.push_back(&m);

                if (row < static_cast<int>(list.size()))
                    selectedMember = list[row]->getId();
                return;
            }

            if (selectedMember >= 0) {
                if (inside(mx, my, x + sc(230), 622, sc(120), 42)) {
                    setDialog(EDIT_MEMBER, selectedMember);
                    return;
                }
                if (inside(mx, my, x + sc(360), 622, sc(120), 42)) {
                    setDialog(DELETE_MEMBER, selectedMember);
                    return;
                }
            }
        }

        if (page == LOANS) {
            int x = 245;
            if (inside(mx, my, x, 165, sc(160), 46)) {
                setDialog(BORROW);
                return;
            }
            if (inside(mx, my, x + sc(175), 165, sc(160), 46)) {
                setDialog(RETURN_BOOK);
                return;
            }
        }

        if (page == SETTINGS) {
            int x = 245;

            // Odunc suresi hizli ayarlari
            if (inside(mx, my, x + 260, 275, 50, 55)) {
                library.setLoanDays(library.getLoanDays() - 1);
                library.save();
                showToast("Odunc suresi " + std::to_string(library.getLoanDays()) + " gun oldu.", true);
                return;
            }
            if (inside(mx, my, x + 316, 275, 50, 55)) {
                library.setLoanDays(library.getLoanDays() + 1);
                library.save();
                showToast("Odunc suresi " + std::to_string(library.getLoanDays()) + " gun oldu.", true);
                return;
            }
            if (inside(mx, my, x + 372, 275, 55, 55)) {
                library.setLoanDays(library.getLoanDays() - 10);
                library.save();
                showToast("Odunc suresi " + std::to_string(library.getLoanDays()) + " gun oldu.", true);
                return;
            }
            if (inside(mx, my, x + 433, 275, 55, 55)) {
                library.setLoanDays(library.getLoanDays() + 10);
                library.save();
                showToast("Odunc suresi " + std::to_string(library.getLoanDays()) + " gun oldu.", true);
                return;
            }
            if (inside(mx, my, x + 500, 275, 150, 55)) {
                setDialog(SETTING);
                return;
            }

            // Gecikme ucreti hizli ayarlari
            if (inside(mx, my, x + 260, 490, 65, 55)) {
                library.setDailyFine(library.getDailyFine() - 1.0);
                library.save();
                showToast("Gecikme ucreti guncellendi.", true);
                return;
            }
            if (inside(mx, my, x + 331, 490, 65, 55)) {
                library.setDailyFine(library.getDailyFine() + 1.0);
                library.save();
                showToast("Gecikme ucreti guncellendi.", true);
                return;
            }
            if (inside(mx, my, x + 500, 490, 150, 55)) {
                setDialog(FINE_SETTING);
                return;
            }
        }
    }

    void clickDialog(int mx, int my) {
        int dw, dh, dx, dy;
        dialogGeom(dw, dh, dx, dy);

        if (inside(mx, my, dx + 465, dy + 330, 105, 46) ||
            ((dialog == DELETE_BOOK || dialog == DELETE_MEMBER) &&
             inside(mx, my, dx + 465, dy + 300, 105, 46))) {
            closeDialog();
            return;
        }

        if (dialog == ADD_BOOK || dialog == EDIT_BOOK) {
            if (inside(mx, my, dx + 30, dy + 90, 540, 48)) activateInput(1);
            else if (inside(mx, my, dx + 30, dy + 170, 540, 48)) activateInput(2);
            else if (dialog == ADD_BOOK && inside(mx, my, dx + 30, dy + 250, 250, 48)) activateInput(3);
            else if (dialog == ADD_BOOK && inside(mx, my, dx + 320, dy + 250, 250, 48)) activateInput(4);
            else if (inside(mx, my, dx + 350, dy + 330, 105, 46)) {
                std::string msg;
                bool ok = false;

                if (dialog == ADD_BOOK) {
                    int y = 0, p = 0;
                    try { y = std::stoi(in3.value); } catch (...) {}
                    try { p = std::stoi(in4.value); } catch (...) {}
                    ok = library.addBook(in1.value, in2.value, y, p, msg);
                } else {
                    ok = library.editBook(dialogId, in1.value, in2.value, msg);
                }

                showToast(msg, ok);
                if (ok) closeDialog();
            }
            return;
        }

        if (dialog == ADD_MEMBER || dialog == EDIT_MEMBER) {
            if (inside(mx, my, dx + 30, dy + 100, 540, 48)) activateInput(1);
            else if (inside(mx, my, dx + 30, dy + 190, 540, 48)) activateInput(2);
            else if (inside(mx, my, dx + 350, dy + 330, 105, 46)) {
                std::string msg;
                bool ok = dialog == ADD_MEMBER
                    ? library.addMember(in1.value, in2.value, msg)
                    : library.editMember(dialogId, in1.value, in2.value, msg);
                showToast(msg, ok);
                if (ok) closeDialog();
            }
            return;
        }

        if (dialog == BORROW || dialog == RETURN_BOOK || dialog == SETTING || dialog == FINE_SETTING) {
            if (inside(mx, my, dx + 30, dy + 110, 540, 48)) activateInput(1);
            else if (dialog == BORROW && inside(mx, my, dx + 30, dy + 195, 540, 48)) activateInput(2);
            else if (inside(mx, my, dx + 350, dy + 330, 105, 46)) {
                std::string msg;
                bool ok = false;

                if (dialog == FINE_SETTING) {
                    double v = 0.0;
                    if (parseDouble(in1.value, v) && v >= MIN_DAILY_FINE && v <= MAX_DAILY_FINE) {
                        library.setDailyFine(v);
                        library.save();
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(2)
                            << "Gecikme ucreti " << library.getDailyFine() << " TL oldu.";
                        msg = oss.str();
                        ok = true;
                    } else {
                        msg = "Lutfen 0 - " + std::to_string(static_cast<int>(MAX_DAILY_FINE)) +
                              " arasinda gecerli bir tutar girin.";
                    }
                } else {
                    try {
                        int a = std::stoi(in1.value);

                        if (dialog == BORROW) {
                            int member = std::stoi(in2.value);
                            ok = library.borrowBook(a, member, msg);
                        } else if (dialog == RETURN_BOOK) {
                            ok = library.returnBook(a, msg);
                        } else { // SETTING
                            if (a >= MIN_LOAN_DAYS && a <= MAX_LOAN_DAYS) {
                                library.setLoanDays(a);
                                library.save();
                                msg = "Odunc suresi " + std::to_string(a) + " gun oldu.";
                                ok = true;
                            } else {
                                msg = "Sure " + std::to_string(MIN_LOAN_DAYS) + "-" +
                                      std::to_string(MAX_LOAN_DAYS) + " gun arasinda olmali.";
                            }
                        }
                    } catch (...) {
                        msg = "Lutfen sadece sayi girin.";
                    }
                }

                showToast(msg, ok);
                if (ok) closeDialog();
            }
            return;
        }

        if (dialog == DELETE_BOOK) {
            if (inside(mx, my, dx + 350, dy + 300, 105, 46)) {
                std::string msg;
                bool ok = library.removeBook(dialogId, msg);
                showToast(msg, ok);
                if (ok) selectedBook = -1;
                closeDialog();
            }
            return;
        }

        if (dialog == DELETE_MEMBER) {
            if (inside(mx, my, dx + 350, dy + 300, 105, 46)) {
                std::string msg;
                bool ok = library.removeMember(dialogId, msg);
                showToast(msg, ok);
                if (ok) selectedMember = -1;
                closeDialog();
            }
            return;
        }
    }

    void handleEvent(SDL_Event& e) {
        if (e.type == SDL_QUIT) {
            running = false;
            return;
        }

        if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                width = e.window.data1;
                height = e.window.data2;
            }
        }

        if (e.type == SDL_TEXTINPUT && dialog != NONE) {
            handleText(e.text.text);
            return;
        }

        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                if (dialog != NONE) closeDialog();
                else running = false;
                return;
            }

            if (dialog != NONE && e.key.keysym.sym == SDLK_BACKSPACE) {
                handleBackspace();
                return;
            }

            if (dialog == NONE && (e.key.keysym.sym == SDLK_BACKSPACE)) {
                if (!search.empty()) {
                    size_t n = search.size();
                    while (n > 0) {
                        --n;
                        if ((static_cast<unsigned char>(search[n]) & 0xC0) != 0x80) {
                            search.erase(n);
                            break;
                        }
                    }
                }
                return;
            }

            if (e.key.keysym.sym == SDLK_RETURN && dialog != NONE) {
                // Onay tusuna basma kolayligi (Enter tusu = ONAYLA/KAYDET butonuna tikla).
                int dw = 600, dh = 400;
                int dx = (width - dw) / 2;
                int dy = (height - dh) / 2;
                int buttonTop = (dialog == DELETE_BOOK || dialog == DELETE_MEMBER) ? dy + 300 : dy + 330;
                clickDialog(dx + 400, buttonTop + 23);
                return;
            }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            int mx = e.button.x;
            int my = e.button.y;

            if (dialog != NONE) {
                clickDialog(mx, my);
                return;
            }

            // Arama kutulari: imlec iceri girdiginde klavye acilir.
            if (page == BOOKS && inside(mx, my, 245, 165, 440, 46)) {
                SDL_StartTextInput();
                in1.active = true;
                in2.active = in3.active = in4.active = false;
                return;
            }
            if (page == MEMBERS && inside(mx, my, 245, 165, 440, 46)) {
                SDL_StartTextInput();
                in1.active = true;
                in2.active = in3.active = in4.active = false;
                return;
            }

            clickNormal(mx, my);
        }
    }

    // Arama kutulari icin ayri metin isleme.
    void handleSearchText(const char* txt) {
        if (dialog != NONE) return;
        if (page != BOOKS && page != MEMBERS) return;
        if (!in1.active) return;

        std::string add = txt ? txt : "";
        if (search.size() + add.size() <= 100) search += add;
    }

public:
    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            return false;
        }

        if (TTF_Init() != 0) {
            SDL_Quit();
            return false;
        }

        window = SDL_CreateWindow(
            "Kutuphane Yonetim Sistemi - SDL2",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

        if (!renderer) {
            SDL_DestroyWindow(window);
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

        if (!font.load(18)) {
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        return true;
    }

    void run() {
        Uint32 last = SDL_GetTicks();

        while (running) {
            SDL_Event e;

            while (SDL_PollEvent(&e)) {
                // Arama kutusuna yazma, normal dialog inputundan farkli.
                if (e.type == SDL_TEXTINPUT && dialog == NONE &&
                    (page == BOOKS || page == MEMBERS) && in1.active) {
                    handleSearchText(e.text.text);
                    continue;
                }

                handleEvent(e);
            }

            Uint32 now = SDL_GetTicks();
            if (now - last >= 1000) {
                last = now;
            }

            render();
            SDL_Delay(8);
        }
    }

    void shutdown() {
        library.save();
        font.close();

        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);

        renderer = nullptr;
        window = nullptr;

        TTF_Quit();
        SDL_Quit();
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    App app;

    if (!app.init()) {
        // SDL/TTF baslatilamazsa sessizce kapanmak yerine kullaniciya bilgi ver.
        // Android'de konsol gorunmeyebilir; yine de hata kodu donuyor.
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
