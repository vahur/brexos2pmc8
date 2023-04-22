#include <pthread.h>
#include "mongoose.h"

class WebServer {
    pthread_t m_thread;
    int m_threadCreateStatus;
    mg_mgr m_mgr;

public:
    WebServer(): m_threadCreateStatus(0) {
        mg_mgr_init(&m_mgr);
    }

    ~WebServer() {
        if (m_threadCreateStatus == 0) {
            pthread_cancel(m_thread);
            pthread_join(m_thread, NULL);
        }

        mg_mgr_free(&m_mgr);
    }

    bool init(const char *listenOn) {
        mg_http_listen(&m_mgr, listenOn, eventHandler, this);
        m_threadCreateStatus = pthread_create(&m_thread, NULL, threadProc, this);
        return m_threadCreateStatus == 0;
    }

private:
    static void *threadProc(void *arg) {
        ((WebServer *) arg)->loop();
        return NULL;
    }

    static void eventHandler(mg_connection *cnn, int ev, void *data, void *server) {
        ((WebServer *) server)->event(cnn, ev, data);
    }

    void loop() {
        while (1) {
            mg_mgr_poll(&m_mgr, 1000);
        }
    }

    void event(mg_connection *cnn, int ev, void *data) {
    }
};
