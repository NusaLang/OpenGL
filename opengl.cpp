#include "plugin_abi.h"

#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace {

struct Jendela {
    Display* dpy = nullptr;
    Window win = 0;
    GLXContext ctx = nullptr;
    Colormap cmap = 0;
    bool harus_tutup = false;
    int lebar = 0;
    int tinggi = 0;
};

std::mutex g_mu;
std::unordered_map<int, Jendela> g_jendela;
int g_next_id = 1;

NsValue nsNull() { NsValue v{}; v.type = NS_NULL; return v; }
NsValue nsBool(bool b) { NsValue v{}; v.type = NS_BOOL; v.boolean = b ? 1 : 0; return v; }
NsValue nsNum(double n) { NsValue v{}; v.type = NS_NUMBER; v.number = n; return v; }
NsValue nsStr(const std::string& s) {
    NsValue v{};
    v.type = NS_STRING;
    v.str = static_cast<char*>(malloc(s.size() + 1));
    memcpy(v.str, s.data(), s.size());
    v.str[s.size()] = 0;
    v.str_len = static_cast<int>(s.size());
    return v;
}
NsValue nsErr(const std::string& msg) { return nsStr(std::string("ERR:") + msg); }

bool argNum(int argc, const NsValue* argv, int i, double& out) {
    if (i >= argc || argv[i].type != NS_NUMBER) return false;
    out = argv[i].number;
    return true;
}
bool argStr(int argc, const NsValue* argv, int i, std::string& out) {
    if (i >= argc || argv[i].type != NS_STRING) return false;
    out.assign(argv[i].str, argv[i].str_len > 0 ? static_cast<size_t>(argv[i].str_len) : strlen(argv[i].str));
    return true;
}

NsValue gl_buka_jendela(int argc, const NsValue* argv) {
    double lebar_d = 640, tinggi_d = 480;
    std::string judul = "Nusantara OpenGL";
    argNum(argc, argv, 0, lebar_d);
    argNum(argc, argv, 1, tinggi_d);
    argStr(argc, argv, 2, judul);
    int lebar = static_cast<int>(lebar_d);
    int tinggi = static_cast<int>(tinggi_d);

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return nsErr("gak bisa buka X display (butuh $DISPLAY / server X aktif)");

    static int attrs[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None};
    XVisualInfo* vi = glXChooseVisual(dpy, DefaultScreen(dpy), attrs);
    if (!vi) { XCloseDisplay(dpy); return nsErr("glXChooseVisual gagal (GPU/driver gak support GLX RGBA+depth)"); }

    Window root = RootWindow(dpy, vi->screen);
    Colormap cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);

    XSetWindowAttributes swa{};
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask;

    Window win = XCreateWindow(dpy, root, 0, 0, lebar, tinggi, 0, vi->depth, InputOutput,
                                vi->visual, CWColormap | CWEventMask, &swa);
    XStoreName(dpy, win, judul.c_str());

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    XMapWindow(dpy, win);

    GLXContext ctx = glXCreateContext(dpy, vi, nullptr, GL_TRUE);
    XFree(vi);
    if (!ctx) { XDestroyWindow(dpy, win); XCloseDisplay(dpy); return nsErr("glXCreateContext gagal"); }

    glXMakeCurrent(dpy, win, ctx);

    Jendela j;
    j.dpy = dpy; j.win = win; j.ctx = ctx; j.cmap = cmap;
    j.lebar = lebar; j.tinggi = tinggi;

    std::lock_guard<std::mutex> lock(g_mu);
    int id = g_next_id++;
    g_jendela[id] = j;
    return nsNum(id);
}

Jendela* cari(int id) {
    auto it = g_jendela.find(id);
    return it == g_jendela.end() ? nullptr : &it->second;
}

NsValue gl_tutup_jendela(int argc, const NsValue* argv) {
    double id_d;
    if (!argNum(argc, argv, 0, id_d)) return nsBool(false);
    std::lock_guard<std::mutex> lock(g_mu);
    Jendela* j = cari(static_cast<int>(id_d));
    if (!j) return nsBool(false);
    glXMakeCurrent(j->dpy, None, nullptr);
    glXDestroyContext(j->dpy, j->ctx);
    XDestroyWindow(j->dpy, j->win);
    XFreeColormap(j->dpy, j->cmap);
    XCloseDisplay(j->dpy);
    g_jendela.erase(static_cast<int>(id_d));
    return nsBool(true);
}

NsValue gl_poll_events(int argc, const NsValue* argv) {
    double id_d;
    if (!argNum(argc, argv, 0, id_d)) return nsStr("[]");
    std::lock_guard<std::mutex> lock(g_mu);
    Jendela* j = cari(static_cast<int>(id_d));
    if (!j) return nsStr("[]");

    std::string out = "[";
    bool first = true;
    XEvent ev;
    while (XPending(j->dpy)) {
        XNextEvent(j->dpy, &ev);
        if (!first) out += ",";
        first = false;
        if (ev.type == ClientMessage) {
            j->harus_tutup = true;
            out += "{\"tipe\":\"tutup\"}";
        } else if (ev.type == ConfigureNotify) {
            j->lebar = ev.xconfigure.width;
            j->tinggi = ev.xconfigure.height;
            out += "{\"tipe\":\"ukuran\",\"lebar\":" + std::to_string(j->lebar) +
                   ",\"tinggi\":" + std::to_string(j->tinggi) + "}";
        } else if (ev.type == KeyPress || ev.type == KeyRelease) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            out += std::string("{\"tipe\":\"tombol\",\"kode\":") + std::to_string(static_cast<long>(ks)) +
                   ",\"turun\":" + (ev.type == KeyPress ? "true" : "false") + "}";
        } else {
            first = true;
        }
    }
    out += "]";
    return nsStr(out);
}

NsValue gl_harus_tutup(int argc, const NsValue* argv) {
    double id_d;
    if (!argNum(argc, argv, 0, id_d)) return nsBool(true);
    std::lock_guard<std::mutex> lock(g_mu);
    Jendela* j = cari(static_cast<int>(id_d));
    return nsBool(!j || j->harus_tutup);
}

NsValue gl_swap_buffers(int argc, const NsValue* argv) {
    double id_d;
    if (!argNum(argc, argv, 0, id_d)) return nsBool(false);
    std::lock_guard<std::mutex> lock(g_mu);
    Jendela* j = cari(static_cast<int>(id_d));
    if (!j) return nsBool(false);
    glXSwapBuffers(j->dpy, j->win);
    return nsBool(true);
}

NsValue gl_ukuran_jendela(int argc, const NsValue* argv) {
    double id_d;
    if (!argNum(argc, argv, 0, id_d)) return nsStr("[0,0]");
    std::lock_guard<std::mutex> lock(g_mu);
    Jendela* j = cari(static_cast<int>(id_d));
    if (!j) return nsStr("[0,0]");
    return nsStr("[" + std::to_string(j->lebar) + "," + std::to_string(j->tinggi) + "]");
}

NsValue gl_clear_color(int argc, const NsValue* argv) {
    double r = 0, g = 0, b = 0, a = 1;
    argNum(argc, argv, 0, r); argNum(argc, argv, 1, g);
    argNum(argc, argv, 2, b); argNum(argc, argv, 3, a);
    glClearColor(static_cast<GLfloat>(r), static_cast<GLfloat>(g), static_cast<GLfloat>(b), static_cast<GLfloat>(a));
    return nsNull();
}

NsValue gl_clear(int argc, const NsValue* argv) {
    (void)argc; (void)argv;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return nsNull();
}

NsValue gl_viewport(int argc, const NsValue* argv) {
    double x = 0, y = 0, w = 0, h = 0;
    argNum(argc, argv, 0, x); argNum(argc, argv, 1, y);
    argNum(argc, argv, 2, w); argNum(argc, argv, 3, h);
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(w), static_cast<GLsizei>(h));
    return nsNull();
}

NsValue gl_versi(int argc, const NsValue* argv) {
    (void)argc; (void)argv;
    const GLubyte* v = glGetString(GL_VERSION);
    return nsStr(v ? reinterpret_cast<const char*>(v) : "");
}

std::vector<double> parseAngkaArray(const std::string& s) {
    std::vector<double> out;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == '-' || c == '+' || c == '.' || isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            i++;
            while (i < s.size() && (isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) i++;
            out.push_back(std::strtod(s.substr(start, i - start).c_str(), nullptr));
        } else {
            i++;
        }
    }
    return out;
}

NsValue gl_warna(int argc, const NsValue* argv) {
    double r = 1, g = 1, b = 1;
    argNum(argc, argv, 0, r); argNum(argc, argv, 1, g); argNum(argc, argv, 2, b);
    glColor3f(static_cast<GLfloat>(r), static_cast<GLfloat>(g), static_cast<GLfloat>(b));
    return nsNull();
}

GLenum modeDariTeks(const std::string& m) {
    if (m == "segitiga") return GL_TRIANGLES;
    if (m == "garis") return GL_LINES;
    if (m == "garis_strip") return GL_LINE_STRIP;
    if (m == "titik") return GL_POINTS;
    if (m == "poligon") return GL_POLYGON;
    if (m == "persegi") return GL_QUADS;
    return GL_LINE_LOOP;
}

NsValue gl_gambar(int argc, const NsValue* argv) {
    std::string mode_teks, json_titik;
    if (!argStr(argc, argv, 0, mode_teks) || !argStr(argc, argv, 1, json_titik))
        return nsErr("gl_gambar(mode, json_titik) butuh 2 teks");
    std::vector<double> pts = parseAngkaArray(json_titik);
    GLenum mode = modeDariTeks(mode_teks);
    glBegin(mode);
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
        glVertex2d(pts[i], pts[i + 1]);
    }
    glEnd();
    return nsNull();
}

}

extern "C" void ns_plugin_init(void* registry, NsRegisterFn reg) {
    reg(registry, "gl_buka_jendela", gl_buka_jendela);
    reg(registry, "gl_tutup_jendela", gl_tutup_jendela);
    reg(registry, "gl_harus_tutup", gl_harus_tutup);
    reg(registry, "gl_poll_events", gl_poll_events);
    reg(registry, "gl_swap_buffers", gl_swap_buffers);
    reg(registry, "gl_ukuran_jendela", gl_ukuran_jendela);
    reg(registry, "gl_clear_color", gl_clear_color);
    reg(registry, "gl_clear", gl_clear);
    reg(registry, "gl_viewport", gl_viewport);
    reg(registry, "gl_versi", gl_versi);
    reg(registry, "gl_warna", gl_warna);
    reg(registry, "gl_gambar", gl_gambar);
}

extern "C" int nusa_abi_version(void) { return NS_PLUGIN_ABI_VERSION; }
