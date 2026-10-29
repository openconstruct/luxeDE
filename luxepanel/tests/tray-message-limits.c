/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../plugins/tray.c"
void x11_root_watch_window(Window window) { (void)window; }
static guint count(BalloonMessage *msg)
{ guint n=0;for(;msg;msg=msg->flink)n++;return n; }
int main(void)
{
    TrayPlugin tr={0};TrayClient clients[5]={{0}};
    for(int i=0;i<5;i++){clients[i].window=i+1;clients[i].client_flink=i<4?&clients[i+1]:NULL;}
    tr.client_list=clients;
    XClientMessageEvent ev={0};ev.data.l[2]=0;ev.data.l[3]=65536;
    for(int client=1;client<=4;client++){
        for(int id=0;id<8;id++){
            ev.window=client;ev.data.l[4]=id;balloon_message_begin_event(&tr,&ev);
        }
        if(client==1){ev.data.l[4]=99;balloon_message_begin_event(&tr,&ev);g_assert_cmpuint(count(tr.incomplete_messages),==,8);}
    }
    g_assert_cmpuint(count(tr.incomplete_messages),==,32);
    g_assert_cmpint(tr.incomplete_messages->timeout,==,10000);
    ev.window=5;ev.data.l[4]=1;balloon_message_begin_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,32);
    tr.incomplete_messages->deadline=g_get_monotonic_time()-1;
    ev.data.l[4]=2;balloon_message_begin_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,32);
    g_assert_cmpuint(tr.incomplete_messages->window,==,5);
    /* Replacing a duplicate is allowed even while the budget is full. */
    ev.window=1;ev.data.l[4]=0;balloon_message_begin_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,32);
    ev.data.l[2]=0;balloon_message_cancel_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,31);
    ev.window=5;ev.data.l[2]=2500;ev.data.l[4]=3;balloon_message_begin_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,32);
    g_assert_cmpint(tr.incomplete_messages->timeout,==,2500);
    for(int client=1;client<=5;client++)balloon_incomplete_message_remove(&tr,client,TRUE,0);
    g_assert_null(tr.incomplete_messages);
    for(int i=0;i<31;i++){
        BalloonMessage *msg=g_new0(BalloonMessage,1);msg->window=100+i;msg->string=g_strdup("queued");
        msg->flink=tr.messages;tr.messages=msg;
    }
    ev.window=5;ev.data.l[4]=50;balloon_message_begin_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,1);
    ev.data.l[4]=51;balloon_message_begin_event(&tr,&ev);
    g_assert_cmpuint(count(tr.incomplete_messages),==,1);
    balloon_incomplete_message_remove(&tr,5,TRUE,0);
    while(tr.messages){BalloonMessage *msg=tr.messages;tr.messages=msg->flink;balloon_message_free(msg);}
    return 0;
}
