/* Hold the first wallpaper decode until the concurrent activation is sent. */
#define _GNU_SOURCE
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

gboolean gdk_pixbuf_loader_write(GdkPixbufLoader *loader, const guchar *bytes,
                                 gsize count, GError **error)
{
    static gboolean (*real_write)(GdkPixbufLoader *, const guchar *, gsize, GError **);
    static int gated;
    if (!real_write) real_write=dlsym(RTLD_NEXT,"gdk_pixbuf_loader_write");
    const char *ready=getenv("FRAME_TEST_GATE_READY");
    const char *release=getenv("FRAME_TEST_GATE_RELEASE");
    if (!gated && ready && release) {
        gated=1;
        int fd=open(ready,O_WRONLY|O_CREAT|O_CLOEXEC,0600);
        if (fd>=0) close(fd);
        for (int i=0;i<200 && access(release,F_OK)!=0;i++) usleep(50000);
    }
    return real_write(loader,bytes,count,error);
}
