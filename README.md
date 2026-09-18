# OpenGL

Wrapper OpenGL (GLX + Xlib) buat Nusantara. Butuh server X aktif (`$DISPLAY`).

## Pasang

```
nusa get github.com/NusaLang/OpenGL
```

atau taruh manual di `nusantara_modules/OpenGL/`. Plugin native (`opengl.so`)
udah kebundel prebuilt buat Linux x86-64. Rebuild kalau perlu:

```bash
g++ -std=c++17 -shared -fPIC -O2 -Wall -Wextra -I. opengl.cpp -o opengl.so -lGL -lX11
```

## Pakai

```
buat OGL = impor("OpenGL");

buat w = OGL.BukaJendela(640, 480, "contoh");
w.WarnaLatar(0.1, 0.1, 0.15, 1);

selama !w.HarusTutup() {
    buat events = w.PollEvents();
    untuk (buat i = 0; i < panjang(events); i = i + 1) {
        jika events[i]["tipe"] == "tombol" && events[i]["turun"] {
            cetak("tombol ditekan, kode: " + ke_teks(events[i]["kode"]));
        }
    }

    w.Bersihkan();
    w.Warna(1, 0.3, 0.3);
    w.Gambar("segitiga", [0, 0.5, -0.5, -0.5, 0.5, -0.5]);
    w.SwapBuffers();
}

w.Tutup();
```

`BukaJendela(lebar, tinggi, judul)` lempar teks error kalau gagal (mis. gak
ada `$DISPLAY`). `PollEvents()` balikin larik peta:

```
{"tipe": "tutup"}
{"tipe": "ukuran", "lebar": N, "tinggi": N}
{"tipe": "tombol", "kode": N, "turun": benar|salah}
```

`kode` itu X11 keysym (`X11/keysym.h`) -- `XK_Escape` = 65307, `XK_space` =
32, huruf pakai kode ASCII biasa.

`Gambar(mode, titik)` -- `mode` salah satu dari `"segitiga"`, `"garis"`,
`"garis_strip"`, `"titik"`, `"poligon"`, `"persegi"` (default `GL_LINE_LOOP`
kalau gak dikenali). `titik` larik flat `[x1, y1, x2, y2, ...]` di NDC
(-1..1). `Warna(r,g,b)` ngatur warna gambar berikutnya.

Method lain: `Ukuran()` (`[lebar, tinggi]`), `Viewport(x,y,w,h)`,
`Versi()` (`glGetString(GL_VERSION)`), `WarnaLatar(r,g,b,a)` buat
`Bersihkan()`.

Ini `glBegin`/`glVertex` (legacy, compatibility profile), bukan core
profile/VBO/shader.

## Lisensi

MIT
