/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "panel.h"
#include "cfg.h"
#include <X11/Xutil.h>
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <string>
#ifdef __GLIBC__
#include <malloc.h>
#endif

/* Private-display diagnostic controller. Never authenticates, starts a DM,
 * executes a session command or touches the host's user configuration. */
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    Cfg cfg;
    const std::string theme=argv[1];
    if(!cfg.readConf(theme+"/luxedm.theme")) return 3;
    Display *display=XOpenDisplay(nullptr);
    if(!display) return 4;
    std::cout<<"connected"<<std::endl;
    Panel *panel=nullptr;
    std::string command;
    while(std::getline(std::cin,command)) {
        if(command=="create") {
            assert(!panel);
            panel=new Panel(display,DefaultScreen(display),DefaultRootWindow(display),&cfg,theme,Panel::Mode_DM);
            panel->SetName("test-user");panel->SwitchSession();
        } else if(command=="open") {
            assert(panel);panel->setBackground();panel->OpenPanel();
        } else if(command=="close") {
            assert(panel);panel->ClosePanel();
        } else if(command=="release") {
            assert(panel);
            const auto name=panel->GetName(),session=panel->getSession();
            panel->ReleaseSessionGraphics();
            assert(panel->GetName()==name && panel->getSession()==session);
        } else if(command=="release-visible") {
            assert(panel);panel->ReleaseSessionGraphics();
        } else if(command=="message") {
            assert(panel);panel->Message("Session failed — try again");
        } else if(command=="destroy") {
            assert(panel);delete panel;panel=nullptr;
        } else if(command=="trim") {
#ifdef __GLIBC__
            malloc_trim(0);
#endif
        } else if(command=="quit") break;
        else return 5;
        XSync(display,False);
        std::cout<<command<<std::endl;
    }
    if(panel) {panel->ClosePanel();delete panel;}
    XCloseDisplay(display);
}
