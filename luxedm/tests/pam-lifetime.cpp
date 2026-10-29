/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <string>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include "panel.h"
#include "app.h"
#include "cfg.h"
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cstdint>
#include <security/pam_appl.h>
#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

App *LoginApp=nullptr; /* app.cpp's callback is linked, never start an App. */
extern int conv(int,const pam_message **,pam_response **,void *);

static void key(Display *display,Window window,KeySym symbol)
{
    XEvent event{};
    event.xkey.type=KeyPress;event.xkey.display=display;
    event.xkey.window=window;event.xkey.root=DefaultRootWindow(display);event.xkey.same_screen=True;
    event.xkey.keycode=XKeysymToKeycode(display,symbol);
    assert(event.xkey.keycode);XPutBackEvent(display,&event);
}

static void free_response(pam_response *response)
{
    if(response->resp) {
        std::memset(response->resp,0,std::strlen(response->resp));
        std::free(response->resp);
    }
    std::free(response);
}

static uint64_t pixels(Display *display,Pixmap pixmap)
{
    Window root;int x,y;unsigned width,height,border,depth;
    assert(XGetGeometry(display,pixmap,&root,&x,&y,&width,&height,&border,&depth));
    XImage *image=XGetImage(display,pixmap,0,0,width,height,AllPlanes,ZPixmap);assert(image);
    uint64_t hash=14695981039346656037ULL;
    for(unsigned row=0;row<height;row++) for(unsigned col=0;col<width;col++) {
        hash^=XGetPixel(image,col,row);hash*=1099511628211ULL;
    }
    XDestroyImage(image);return hash;
}

struct PanelLifetimeTest {
static void run(const char *theme)
{
    Display *display=XOpenDisplay(nullptr);assert(display);
    Cfg cfg;
    assert(cfg.parseOption("sessions Luxe"));
    assert(cfg.readConf(std::string(theme)+"/luxedm.theme"));
    {
        Panel panel(display,DefaultScreen(display),DefaultRootWindow(display),&cfg,theme,Panel::Mode_DM);
        Panel *panel_pointer=&panel;
        GreeterPamContext context{&panel_pointer,nullptr,true};
        panel.SetName("test-user");panel.SwitchSession();
        const auto session=panel.getSession();
        // Autologin selects a nonempty session before any greeter window exists.
        assert(session=="Luxe" && !panel.Root && !panel.Win);
        XSync(display,False); // Force any invalid-window drawing request to fail.
        // Synthetic extended viewports exercise the actual mirror renderer/free
        // loops. This is not a physical multi-output hardware acceptance test.
        panel.FreePixmaps();
        const unsigned width=DisplayWidth(display,DefaultScreen(display))/2;
        panel.viewport=Rectangle(0,0,width,768);
        Panel::MirrorSurface mirror;mirror.viewport=Rectangle(width,0,width,768);
        panel.mirrors.push_back(mirror);panel.PrepareGraphics();
        panel.setBackground();panel.OpenPanel();
        const auto primary=panel.PanelPixmap,secondary=panel.mirrors[0].panel_pixmap;
        const auto primary_pixels=pixels(display,primary),mirror_pixels=pixels(display,secondary);
        Display *probe=XOpenDisplay(nullptr);assert(probe);
        assert(XGrabKeyboard(probe,DefaultRootWindow(probe),False,GrabModeAsync,GrabModeAsync,CurrentTime)==AlreadyGrabbed);
        panel.ReleaseSessionGraphics();
        assert(panel.PanelPixmap==primary && panel.mirrors[0].panel_pixmap==secondary);
        panel.ClosePanel();panel.ClosePanel();panel.ReleaseSessionGraphics();
        // Selection also remains safe after the last greeter is destroyed.
        panel.ShowSession(); XSync(display,False);
        assert(XGrabKeyboard(probe,DefaultRootWindow(probe),False,GrabModeAsync,GrabModeAsync,CurrentTime)==GrabSuccess);
        XUngrabKeyboard(probe,CurrentTime);XSync(probe,False);XCloseDisplay(probe);
        assert(!panel.PanelPixmap && !panel.BackgroundPixmap);
        assert(!panel.mirrors[0].panel_pixmap && !panel.mirrors[0].background);
        assert(panel.GetName()=="test-user" && panel.getSession()==session);

        pam_response *response=nullptr;
        assert(conv(0,nullptr,&response,&context)==PAM_CONV_ERR);
        pam_message invalid{99,"invalid"};const pam_message *messages[]={&invalid};
        assert(conv(1,messages,&response,&context)==PAM_CONV_ERR && response==nullptr);
        Panel *missing=nullptr;
        assert(conv(1,messages,&response,&missing)==PAM_CONV_ERR && response==nullptr);
        pam_message username{PAM_PROMPT_ECHO_ON,"Username"};messages[0]=&username;
        key(display,panel.Win,XK_Return);
        assert(conv(1,messages,&response,&context)==PAM_SUCCESS);
        assert(response && std::strcmp(response->resp,"test-user")==0);free_response(response);
        // Echo-on second factors use the name field temporarily but do not
        // overwrite the PAM account identity once it has been established.
        context.identity_is_bound_hint=true;
        key(display,panel.Win,XK_Return);key(display,panel.Win,XK_4);key(display,panel.Win,XK_2);
        XEvent clear{};clear.xkey.type=KeyPress;clear.xkey.display=display;
        clear.xkey.window=panel.Win;clear.xkey.root=DefaultRootWindow(display);clear.xkey.same_screen=True;
        clear.xkey.keycode=XKeysymToKeycode(display,XK_u);clear.xkey.state=ControlMask;
        XPutBackEvent(display,&clear);
        assert(conv(1,messages,&response,&context)==PAM_SUCCESS);
        assert(response && std::strcmp(response->resp,"24")==0);free_response(response);
        assert(panel.GetName()=="test-user");
        pam_message info{PAM_TEXT_INFO,"Informational only"};messages[0]=&info;
        assert(conv(1,messages,&response,&context)==PAM_SUCCESS);
        assert(response && response[0].resp==nullptr);free_response(response);
        assert(panel.Win && panel.Root && panel.mirrors[0].panel && panel.mirrors[0].root);
        assert(panel.PanelPixmap && panel.mirrors[0].panel_pixmap);
        assert(pixels(display,panel.PanelPixmap)==primary_pixels);
        assert(pixels(display,panel.mirrors[0].panel_pixmap)==mirror_pixels);
        assert(panel.getSession()==session);
        panel.ClosePanel();panel.ReleaseSessionGraphics();
        pam_message password{PAM_PROMPT_ECHO_OFF,"Password"};messages[0]=&password;
        // Queued private-X input, reverse order because XPutBackEvent prepends.
        key(display,panel.Win,XK_Return);key(display,panel.Win,XK_c);key(display,panel.Win,XK_b);key(display,panel.Win,XK_a);
        assert(conv(1,messages,&response,&context)==PAM_SUCCESS);
        assert(response && std::strcmp(response->resp,"abc")==0);free_response(response);
        assert(panel.GetPasswd().empty());
        pam_message first_factor{PAM_PROMPT_ECHO_OFF,"First factor"};
        pam_message second_factor{PAM_PROMPT_ECHO_OFF,"Second factor"};
        key(display,panel.Win,XK_Return);key(display,panel.Win,XK_c);key(display,panel.Win,XK_b);key(display,panel.Win,XK_a);
        const pam_message *factor[]={&first_factor};
        assert(conv(1,factor,&response,&context)==PAM_SUCCESS);
        assert(response && std::strcmp(response[0].resp,"abc")==0);free_response(response);
        key(display,panel.Win,XK_Return);key(display,panel.Win,XK_f);key(display,panel.Win,XK_e);key(display,panel.Win,XK_d);
        factor[0]=&second_factor;
        assert(conv(1,factor,&response,&context)==PAM_SUCCESS);
        assert(response && std::strcmp(response[0].resp,"def")==0);free_response(response);
        assert(panel.GetPasswd().empty());
        pam_message invalid_after_secret{99,"invalid after secret"};
        const pam_message *partial[]={&password,&invalid_after_secret};
        key(display,panel.Win,XK_Return);key(display,panel.Win,XK_c);
        key(display,panel.Win,XK_b);key(display,panel.Win,XK_a);
        assert(conv(2,partial,&response,&context)==PAM_CONV_ERR);
        assert(response==nullptr && panel.GetPasswd().empty());
        panel.WrongPassword(0);panel.ClosePanel();panel.ReleaseSessionGraphics();
        panel.Message("Failed to execute login command");
        assert(panel.Root && panel.mirrors[0].root);panel.ClosePanel();panel.ReleaseSessionGraphics();
        panel.OpenPanel();panel.ClosePanel();panel.ReleaseSessionGraphics();
    }
    {
        Panel lock(display,DefaultScreen(display),DefaultRootWindow(display),&cfg,theme,Panel::Mode_Lock);
        lock.setBackground();lock.ClosePanel();
        const auto pixmap=lock.PanelPixmap;lock.ReleaseSessionGraphics();
        assert(pixmap && lock.PanelPixmap==pixmap); // No lock resource teardown.
    }
    XSync(display,False);XCloseDisplay(display);
    std::cout<<"PASS: production PAM callback, reopen/password clearing, mirror lifetime, lock guard\n";
}
};

int main(int argc,char **argv)
{
    assert(argc==2);PanelLifetimeTest::run(argv[1]);
}
