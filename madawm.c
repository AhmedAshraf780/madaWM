// miniwm.c - Fixed Minimal Window Manager
// 2 workspaces: WS0 for terminals, WS1 for browsers
// Build: gcc -o miniwm miniwm.c -lX11
// Run: startx /path/to/miniwm -- :1

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#define WORKSPACES 2
#define BORDER_WIDTH 2
#define BORDER_FOCUS 0x4A90D9    // Blue
#define BORDER_UNFOCUS 0x333333  // Dark gray

typedef struct Client {
    Window w;
    int workspace;
    struct Client *next;
} Client;

Display *dpy;
Window root;
int screen_w, screen_h;
Client *clients = NULL;
int cur_ws = 0;
int running = 1;
Atom wm_protocols, wm_delete_window, wm_take_focus;

// Terminal classes for WS0
const char *terminal_classes[] = {
    "xterm", "XTerm", "URxvt", "urxvt", "Terminal",
    "kitty", "Kitty", "Alacritty", "alacritty", "St", "st", NULL
};

// Browser classes for WS1
const char *browser_classes[] = {
    "firefox", "Firefox", "Chromium", "chromium",
    "Google-chrome", "google-chrome", "Brave-browser", NULL
};

void die(const char *s) {
    perror(s);
    exit(1);
}

int match_class(Window w, const char **classes) {
    XClassHint ch;
    if (!XGetClassHint(dpy, w, &ch)) return 0;

    int match = 0;
    if (ch.res_class || ch.res_name) {
        for (const char **p = classes; *p; ++p) {
            if ((ch.res_class && strcasecmp(ch.res_class, *p) == 0) ||
                (ch.res_name && strcasecmp(ch.res_name, *p) == 0)) {
                match = 1;
                break;
            }
        }
    }

    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
    return match;
}

int get_window_workspace(Window w) {
    if (match_class(w, terminal_classes)) return 0;
    if (match_class(w, browser_classes)) return 1;
    return -1; // Not allowed
}

Client* find_client(Window w) {
    for (Client *c = clients; c; c = c->next)
        if (c->w == w) return c;
    return NULL;
}

void add_client(Window w, int workspace) {
    Client *c = malloc(sizeof(Client));
    c->w = w;
    c->workspace = workspace;
    c->next = clients;
    clients = c;

    XSetWindowBorderWidth(dpy, w, BORDER_WIDTH);
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask | PropertyChangeMask);
}

void remove_client(Window w) {
    Client **pp = &clients;
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

void set_border(Window w, unsigned long color) {
    XSetWindowBorder(dpy, w, color);
}

int supports_protocol(Window w, Atom protocol) {
    Atom *protocols;
    int count, ret = 0;
    if (XGetWMProtocols(dpy, w, &protocols, &count)) {
        for (int i = 0; i < count; i++) {
            if (protocols[i] == protocol) {
                ret = 1;
                break;
            }
        }
        XFree(protocols);
    }
    return ret;
}

void set_focus(Window w) {
    if (w == None) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        return;
    }

    // Unfocus all windows first
    for (Client *c = clients; c; c = c->next) {
        if (c->workspace == cur_ws)
            set_border(c->w, BORDER_UNFOCUS);
    }

    set_border(w, BORDER_FOCUS);
    XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
    XRaiseWindow(dpy, w);

    if (supports_protocol(w, wm_take_focus)) {
        XClientMessageEvent ev = {0};
        ev.type = ClientMessage;
        ev.window = w;
        ev.message_type = wm_protocols;
        ev.format = 32;
        ev.data.l[0] = wm_take_focus;
        ev.data.l[1] = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, (XEvent *)&ev);
    }
}

void arrange() {
    // Count windows in current workspace
    int count = 0;
    for (Client *c = clients; c; c = c->next)
        if (c->workspace == cur_ws) count++;

    if (count == 0) {
        set_focus(None);
        return;
    }

    // Tile windows horizontally
    int tile_w = screen_w / count;
    int i = 0;
    Client *focused = NULL;

    for (Client *c = clients; c; c = c->next) {
        if (c->workspace == cur_ws) {
            int x = i * tile_w;
            int w = (i == count - 1) ? (screen_w - x) : tile_w;
            XMoveResizeWindow(dpy, c->w, x, 0,
                            w - 2 * BORDER_WIDTH,
                            screen_h - 2 * BORDER_WIDTH);
            XMapWindow(dpy, c->w);
            if (i == 0) focused = c;
            i++;
        } else {
            XUnmapWindow(dpy, c->w);
        }
    }

    if (focused) set_focus(focused->w);
    XSync(dpy, False);
}

void focus_next() {
    Client *cur = NULL, *first = NULL, *next = NULL;
    Window focused_w;
    int revert;
    XGetInputFocus(dpy, &focused_w, &revert);

    // Find current focused and build list
    for (Client *c = clients; c; c = c->next) {
        if (c->workspace != cur_ws) continue;
        if (!first) first = c;
        if (cur) {
            next = c;
            break;
        }
        if (c->w == focused_w) cur = c;
    }

    // Cycle to next (or wrap to first)
    if (next) set_focus(next->w);
    else if (first) set_focus(first->w);
}

void focus_prev() {
    Client *cur = NULL, *first = NULL, *prev = NULL, *last = NULL;
    Window focused_w;
    int revert;
    XGetInputFocus(dpy, &focused_w, &revert);

    for (Client *c = clients; c; c = c->next) {
        if (c->workspace != cur_ws) continue;
        if (!first) first = c;
        if (c->w == focused_w) cur = c;
        else if (!cur) prev = c;
        last = c;
    }

    // Cycle to prev (or wrap to last)
    if (prev) set_focus(prev->w);
    else if (last) set_focus(last->w);
}

void change_ws(int ws) {
    if (ws < 0 || ws >= WORKSPACES || ws == cur_ws) return;
    cur_ws = ws;
    arrange();
}

void kill_focused() {
    Window focused_w;
    int revert;
    XGetInputFocus(dpy, &focused_w, &revert);
    if (focused_w == None || focused_w == root) return;

    if (supports_protocol(focused_w, wm_delete_window)) {
        XClientMessageEvent ev = {0};
        ev.type = ClientMessage;
        ev.window = focused_w;
        ev.message_type = wm_protocols;
        ev.format = 32;
        ev.data.l[0] = wm_delete_window;
        ev.data.l[1] = CurrentTime;
        XSendEvent(dpy, focused_w, False, NoEventMask, (XEvent *)&ev);
    } else {
        XKillClient(dpy, focused_w);
    }
}

void spawn_cmd(const char *cmd) {
    if (!cmd) return;
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(1);
    }
}

void handle_maprequest(XEvent *e) {
    Window w = e->xmaprequest.window;

    // Check if already managed
    if (find_client(w)) return;

    int ws = get_window_workspace(w);
    if (ws < 0) {
        // Not allowed - kill it
        XKillClient(dpy, w);
        return;
    }

    add_client(w, ws);

    // Switch to appropriate workspace if needed
    if (ws != cur_ws) change_ws(ws);
    else arrange();
}

void handle_unmap(XEvent *e) {
    Window w = e->xunmap.window;
    if (e->xunmap.send_event) { // Ignore synthetic events
        remove_client(w);
        arrange();
    }
}

void handle_destroy(XEvent *e) {
    Window w = e->xdestroywindow.window;
    remove_client(w);
    arrange();
}

void handle_configure_request(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc = {
        .x = ev->x,
        .y = ev->y,
        .width = ev->width,
        .height = ev->height,
        .border_width = BORDER_WIDTH,
        .sibling = ev->above,
        .stack_mode = ev->detail
    };
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
}

void handle_enternotify(XEvent *e) {
    Client *c = find_client(e->xcrossing.window);
    if (c && c->workspace == cur_ws)
        set_focus(c->w);
}

void grab_keys() {
    unsigned int mod = Mod4Mask; // Super key

    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_b), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_1), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_2), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_c), mod | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_q), mod | ShiftMask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_h), mod, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_l), mod, root, True, GrabModeAsync, GrabModeAsync);
}

void setup() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) die("Cannot open display");

    root = DefaultRootWindow(dpy);
    screen_w = DisplayWidth(dpy, DefaultScreen(dpy));
    screen_h = DisplayHeight(dpy, DefaultScreen(dpy));

    // Check if another WM is running
    XSetErrorHandler(NULL);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(dpy, False);

    // Intern atoms
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);

    grab_keys();

    // Set cursor
    XDefineCursor(dpy, root, XCreateFontCursor(dpy, 68));
}

void cleanup() {
    while (clients) {
        Client *c = clients;
        clients = c->next;
        XUnmapWindow(dpy, c->w);
        free(c);
    }
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
            case EnterNotify:
                handle_enternotify(&ev);
                break;
            case KeyPress: {
                KeySym k = XLookupKeysym(&ev.xkey, 0);
                unsigned int state = ev.xkey.state & ~(LockMask | Mod2Mask);

                if (state == Mod4Mask) {
                    if (k == XK_Return) {
                        const char *term = getenv("TERMINAL");
                        if (!term) term = "kitty";
                        spawn_cmd(term);
                    } else if (k == XK_b) {
                        spawn_cmd("firefox");
                    } else if (k == XK_1) {
                        change_ws(0);
                    } else if (k == XK_2) {
                        change_ws(1);
                    } else if (k == XK_h) {
                        focus_prev();
                    } else if (k == XK_l) {
                        focus_next();
                    }
                } else if (state == (Mod4Mask | ShiftMask)) {
                    if (k == XK_c) {
                        kill_focused();
                    } else if (k == XK_q) {
                        running = 0;
                    }
                }
                break;
            }
        }
    }
}

int main() {
    signal(SIGCHLD, SIG_IGN);
    setup();
    run();
    cleanup();
    return 0;
}
