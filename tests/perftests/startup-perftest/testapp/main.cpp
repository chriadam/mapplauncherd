#include <MApplication>
#include <MApplicationWindow>
#include <MApplicationPage>

#ifdef HAVE_MCOMPONENTCACHE   
    #include <MComponentCache>
#endif

#include <MExport>
#include <QFile>
#include <sys/time.h>

void FANGORNLOG(const char* s)
{
    QFile f("/tmp/testapp.log");
    f.open(QIODevice::Append);
    f.write(s, qstrlen(s));
    f.write("\n", 1);
    f.close();
}

void timestamp(const char *s)
{
    timeval tim;
    char msg[80];
    gettimeofday(&tim, NULL);
    snprintf(msg, 80, "%d.%06d %s\n", 
             static_cast<int>(tim.tv_sec), static_cast<int>(tim.tv_usec), s);
    FANGORNLOG(msg);
}

class MyApplicationPage: public MApplicationPage
{
public:
    MyApplicationPage(): MApplicationPage() {}
    virtual ~MyApplicationPage() {}
    void enterDisplayEvent() {
        timestamp("MyApplicationPage::enterDisplayEvent");
    }
};

M_EXPORT int main(int, char**);

int main(int argc, char **argv) {
    timestamp("application main");
    
#ifdef HAVE_MCOMPONENTCACHE   
    MApplication* app = MComponentCache::mApplication(argc, argv);
    MApplicationWindow* w = MComponentCache::mApplicationWindow();
#else
    MApplication* app = new MApplication(argc, argv);
    MApplicationWindow* w = new MApplicationWindow;
#endif
    
    MyApplicationPage p;
    w->show();
    p.appear();
    return app->exec();
}

