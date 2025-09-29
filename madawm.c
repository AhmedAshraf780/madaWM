// miniwm.c
// Minimal tiling wm with 3 workspaces, only terminal & browser allowed.
// Build: gcc -o miniwm miniwm.c -lX11
// Run: startx /path/to/miniwm -- :1   (or from .xinitrc)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define WORKSPACES 3

typedef struct Client {
    Window w;
    struct Client *next;
} Client;

Display *dpy;
Window root;
int screen_w, screen_h;
Client *ws[WORKSPACES];
int cur_ws = 0;
int running = 1;

/* Allowed classes (simple) */
const char *allowed_classes[] = { "XTerm", "xterm", "Terminal", "URxvt", "urxvt", "Firefox", "firefox","kitty","Kitty", NULL };

/* Helpers */
void die(const char *s){ perror(s); exit(1); }

void add_client(Window w) {
    Client *c = malloc(sizeof(Client));
    c->w = w; c->next = ws[cur_ws]; ws[cur_ws] = c;
}

void remove_client_from_ws(Window w, int wsidx) {
    Client **pp = &ws[wsidx];
    while (*pp) {
        if ((*pp)->w == w) {
            Client *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            return;
        }
        pp = &(*pp)->next;
    }
}

int allowed_window(Window w) {
    XClassHint ch;
    if (XGetClassHint(dpy, w, &ch)) {
        if (ch.res_class) {
            for (const char **p = allowed_classes; *p; ++p) {
                if (strcasecmp(ch.res_class, *p) == 0 || strcasecmp(ch.res_name, *p) == 0) {
                    if (ch.res_name) XFree(ch.res_name);
                    if (ch.res_class) XFree(ch.res_class);
                    return 1;
                }
            }
        }
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    }
    return 0;
}

void arrange() {
    Client *c;
    int count = 0;
    for (c = ws[cur_ws]; c; c = c->next) count++;
    if (count == 0) return;
    int i = 0;
    int w = screen_w / count;
    for (c = ws[cur_ws]; c; c = c->next) {
        XMoveResizeWindow(dpy, c->w, i * w, 0, w, screen_h);
        XMapRaised(dpy, c->w);
        i++;
    }
    XFlush(dpy);
}

void focus_next() {
    if (!ws[cur_ws]) return;
    Window first = ws[cur_ws]->w;
    remove_client_from_ws(first, cur_ws);
    add_client(first);
    arrange();
}

void focus_prev() {
    // rotate other direction: move last to front
    Client **pp = &ws[cur_ws];
    if (!*pp || !(*pp)->next) return;
    Client *prev = NULL, *cur = *pp;
    while (cur->next) { prev = cur; cur = cur->next; }
    // cur is last
    prev->next = NULL;
    cur->next = *pp;
    *pp = cur;
    arrange();
}

void change_ws(int idx) {
    if (idx < 0 || idx >= WORKSPACES) return;
    // hide current ws windows
    Client *c;
    for (c = ws[cur_ws]; c; c = c->next) XUnmapWindow(dpy, c->w);
    cur_ws = idx;
    arrange();
}

void spawn_cmd(const char *cmd) {
    if (!cmd) return;
    if (fork() == 0) {
        setsid();
        execl("/bin/sh","sh","-c", cmd, (char*)NULL);
        _exit(0);
    }
}

void handle_maprequest(XEvent *e) {
    Window w = e->xmaprequest.window;
    // if window is not allowed, kill it
    if (!allowed_window(w)) {
        XKillClient(dpy, w);
        return;
    }
    // reparent? we won't reparent; just manage
    add_client(w);
    XSelectInput(dpy, w, StructureNotifyMask | PropertyChangeMask);
    arrange();
}

void handle_unmap(XEvent *e) {
    Window w = e->xunmap.window;
    remove_client_from_ws(w, cur_ws);
    arrange();
}

void handle_destroy(XEvent *e) {
    Window w = e->xdestroywindow.window;
    remove_client_from_ws(w, cur_ws);
    arrange();
}

void handle_configure_request(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges changes;
    changes.x = ev->x; changes.y = ev->y; changes.width = ev->width; changes.height = ev->height;
    changes.border_width = ev->border_width; changes.sibling = ev->above; changes.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &changes);
}

void grab_keys() {
    unsigned int mod = Mod4Mask; // Super
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_b), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_1), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_2), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_3), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_q), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_h), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_l), mod, root, True, GrabModeAsync, GrabModeAsync);
}

void setup() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) die("XOpenDisplay");
    root = DefaultRootWindow(dpy);
    screen_w = DisplayWidth(dpy, DefaultScreen(dpy));
    screen_h = DisplayHeight(dpy, DefaultScreen(dpy));
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    grab_keys();
}

void cleanup() {
    XCloseDisplay(dpy);
}

void run() {
    XEvent ev;
    while (running && !XNextEvent(dpy, &ev)) {
        switch (ev.type) {
            case MapRequest:
                handle_maprequest(&ev);
                break;
            case UnmapNotify:
                handle_unmap(&ev);
                break;
            case DestroyNotify:
                handle_destroy(&ev);
                break;
            case ConfigureRequest:
                handle_configure_request(&ev);
                break;
            case KeyPress: {
                KeySym k = XLookupKeysym(&ev.xkey, 0);
                if (k == XK_Return) {
                    const char *term = getenv("TERMINAL");
                    if (!term) term = "kitty";
                    spawn_cmd(term);
                } else if (k == XK_b) {
                    spawn_cmd("firefox");
                } else if (k == XK_1) change_ws(0);
                else if (k == XK_2) change_ws(1);
                else if (k == XK_3) change_ws(2);
                else if (k == XK_q) running = 0;
                else if (k == XK_h) focus_prev();
                else if (k == XK_l) focus_next();
                break;
            }
            default: break;
        }
    }
}

int main(int argc, char **argv) {
    signal(SIGCHLD, SIG_IGN);
    setup();
    run();
    cleanup();
    return 0;
}
