#ifndef HTTPLIGHTSERVER_H__
#define HTTPLIGHTSERVER_H__

#include <string>
#include <functional>
#include <vector>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <sys/mman.h>
#include <cassert>
#include <pthread.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace http
{


//send/recv/close
//ssl
enum METHOD
{
    XXS_METHOD_UNKNOWN = 0,
    XXS_METHOD_GET,
    XXS_METHOD_POST,
    XXS_METHOD_HEAD,
    XXS_METHOD_PUT,
    XXS_METHOD_DELETE,
    XXS_METHOD_CONNECT,
    XXS_METHOD_TRACE,
    XXS_METHOD_OPTIONS,
};
enum StatusCode 
{
    XXS_OK = 0,
    XXS_AGINE,
    XXS_CANNEL,
    XXS_ERROR,
    XXS_END,//only send can use this option
};

enum HTTPCODE
{
    XXS_CODE_OK = 200,
    XXS_CODE_Partial_Content = 206,
    XXS_CODE_Bad_Request= 400,
    XXS_CODE_Content_Not_Change = 304,
    XXS_CODE_Content_Not_Found = 404,
    XXS_CODE_Forbidden = 403,
    XXS_CODE_Internal_Server_Error = 500,
    XXS_CODE_Temporary_Redirect = 307,
    XXS_CODE_Permanent_Redirect = 308,
    XXS_CODE_Moved_Permanently = 301,

};

typedef struct _XXS_FILE
{
    size_t   age;
    size_t   expires_seconds;
    std::string date;
    std::string etag;
    std::string ftype;

    char    *p;
    size_t   len;

    int fd;
}XXS_FILE;

class XXS_Http_Server;
class FileCache
{
public:

    const XXS_FILE* get(std::string file);

    ~FileCache();
private:
      FileCache(std::string const& rootPath);
//    FileCache(FileChache const&)=delete;
    void monitor(); //in thread
    XXS_FILE* loadfile(std::string const& file);

    template<size_t MaxMemorySize /*Bytes*/>
    class SMem
    {
    public:
        SMem() { memset(ipages, 0, sizeof(char)*npages); memset(ppages, 0, sizeof(char*)*npages);}
        ~SMem() { for (int i = 0; i< npages ; i++) { if (ppages[i]){ free(ppages[i]); ppages[i]=0; } } }
        char* smalloc_less4KB(size_t len) 
        {
            //assert(len < 4096, "can not alloc memory exceed 4KB");
            assert(len < 4096);
            size_t c = len/512 + 1;
            char m = (c == 1) ? 1 : (c == 2) ? 3 : (c == 3) ? 7 : (c == 4) ? 15 : (c == 5) ? 31 : (c == 6) ? 63 : (c == 7) ? 127 : (c == 8) ? 255 : 2;
            size_t i,j;
            for (i=0,j=8; i < npages; i++) { 
                if (ppages[i] && ipages[i] != 255) {
                    char a = ipages[i];
                    j = (m == 2) ? 8 :  \
                        ((a & m) == 0) ? 0 :\
                        ((c<=7)&& (((a>>1) & m) == 0)) ? 1 : \
                        ((c<=6)&& (((a>>2) & m) == 0)) ? 2 : \
                        ((c<=5)&& (((a>>3) & m) == 0)) ? 3 : \
                        ((c<=4)&& (((a>>4) & m) == 0)) ? 4 : \
                        ((c<=3)&& (((a>>5) & m) == 0)) ? 5 : \
                        ((c<=2)&& (((a>>6) & m) == 0)) ? 6 : \
                        ((c<=1)&& (((a>>7) & m) == 0)) ? 7 : 8;
                }
                if (ppages[i] == 0 || j < 8) { break; }
            }
            char *p = 0;
            if (i<npages && ppages[i] == 0) { j=0; ppages[i] =(char*) malloc(4096); ipages[i] = m; p = ppages[i]; *p=m; p++; }
            else if (i<npages && j<8) { p = ppages[i] + 512*j; ipages[i] = ipages[i] | (m<<j); *p = m<<j; p++; }
            
            return p;
        }
        void sfree(char *p) { for (int i=0; i < npages; i++) { if (p >= ppages[i] && p < (ppages[i]+4096)) { ipages[i] = ipages[i] & (~*(p-1)); break; } } }
    private:
        constexpr static size_t npages = MaxMemorySize/4096 + 1; // 4KB page
        unsigned char    ipages[npages]; 
        char*   ppages[npages];
    };

    friend XXS_Http_Server;

private:

    std::string  root;
    SMem<0x4000000> cachepages;  //64MB
    std::map<std::string, XXS_FILE*> cachefiles;
    pthread_rwlock_t rwlock;
    volatile bool mcheckrunning;
    std::thread mth;
};

class Request
{
public:
typedef std::unordered_map<std::string, std::function<void(Request*, std::string const&)>>  RequestHeadersControl;
typedef std::function<StatusCode(Request*)> RequestHandler;
   
public:
    METHOD method();
    bool hasParam();
    bool keepAlive();

    std::string const& read();
    std::string const& readHead(std::string const& key);
    std::string const& readParam(std::string const& key);

    
    void redirectTemporary(std::string const& newLocation);
    void redirectPermanent(std::string const& newLocation);

    void write(std::string const& content){m_httpResponse=content; writeHead("Content-Length", std::to_string(m_httpResponse.length()));}
    void write(std::string && content){m_httpResponse=std::move(content); writeHead("Content-Length", std::to_string(m_httpResponse.length()));}
    void writeHead(std::string const& key, std::string const& val) {
        if (!val.empty()) {
           m_httpResponseHeaders.push_back(key + ": " + val);
        }
    }
    void writeCode(HTTPCODE code) {
        if (XXS_CODE_Bad_Request == code) {
            m_httpResponseStatus = m_version + " " + std::to_string(code) + " Bad Request";
        }
        if (XXS_CODE_OK == code) {
            m_httpResponseStatus = m_version + " " + std::to_string(code) + " OK";
        }
        if (XXS_CODE_Partial_Content == code) {
            m_httpResponseStatus = m_version + " " + std::to_string(code) + " Partial Content";
        }
        if (XXS_CODE_Content_Not_Change== code) {
            m_httpResponseStatus = m_version + " " + std::to_string(code);
        }
    }

    bool donext();

    int socket(){return m_fd;}

private:
    Request(int fd,  XXS_Http_Server* hp): m_fd(fd), m_keepAlive(false), m_currentIdx(0), m_pxxs_http_server(hp) {
        this->initHandlers();
        memset(m_temporarydata, 0, 8);
    }
    StatusCode recv(Request* rq); //will decide the nexts handlers
    StatusCode parse(Request* rq);
    StatusCode handle(Request* rq);
    StatusCode files(Request* rq);
    StatusCode send(Request* rq);

   bool close();
   void clear();
   void initHandlers();
   void selfdestroy();
  
private:
    int m_fd;

    bool m_keepAlive;

    int  m_currentIdx;
    std::vector<RequestHandler> m_handlers;


    size_t m_httpContentIdx;
    size_t m_httpContentLen;

    METHOD      m_method;
    std::string m_path;
    std::string m_version; 
    std::string m_host;
    std::string m_agent;
    std::string m_cookie;
    std::string m_accept;
    std::string m_range;
    std::string m_accept_encoding;
    std::string m_accept_language;
    std::string m_connection;
    std::string m_contentType;
    std::string m_cacheControl;
    std::string m_if_modified_since;
    std::string m_upgrade_insecure_requests;
    std::string m_sec_ch_ua;
    std::string m_sec_ch_ua_mobile;
    std::string m_sec_fetch_site;
    std::string m_sec_fetch_mode;
    std::string m_sec_fetch_user;
    std::string m_sec_fetch_dest;


    std::string m_httpRequest;
    std::string m_httpResponse;
    std::string m_httpResponseStatus;
    std::vector<std::string> m_httpResponseHeaders;

    char m_temporarydata[8];
    XXS_Http_Server *m_pxxs_http_server;
    friend XXS_Http_Server;
};


//connection
//message
//metched handlers   --a--b--c--d-- done/cancal/next
//default self-owned handlers FileService;
//
//read/write
class HttpRequestWorker
{
public:

    HttpRequestWorker(size_t regularWorker, size_t maxWorker=1000):
        m_regularWorker(regularWorker),m_maxWorker(maxWorker) {
        m_workerFlag.resize(maxWorker+1, false); m_workerFlag[0] = true;
        m_workers.push_back(std::thread(&HttpRequestWorker::conductor, this));
    }

    void pushTask(Request* rq);
    ~HttpRequestWorker(){stop();if(m_workers[0].joinable()){m_workers[0].join();}}

private:
    void conductor();
    void work(int id);
    bool isstopped(int id);
    void start(int id);
    void stop(int fid, int lid);
    void stop(){stop(0,-1);}
    size_t leftTasks();
    std::vector<Request*> fetchTasks(size_t max, int workid, int timeout=1);

private:
    size_t  m_regularWorker;
    size_t  m_maxWorker;

    std::deque<Request*>           m_tasks;
    std::condition_variable        m_taskSync;
    std::mutex                     m_taskMutex;
    std::vector<std::thread>       m_workers;
    std::mutex                     m_workerMutex;
    std::vector<bool>              m_workerFlag;
};


typedef struct URITree
{
    std::string name;
    std::vector<std::pair<METHOD, std::function<StatusCode(Request*)>>> vhanders;
    std::vector<std::pair<METHOD, std::function<StatusCode(Request*)>>> vhijacks;

    std::vector<URITree*> vchild;
    URITree *parent;
}URITree;

class Handler
{
public:
   void handle(std::string const& uri, std::function<StatusCode(Request*)> const& handler);
   void handle(METHOD method, std::string const& uri, std::function<StatusCode(Request*)> const& handler);

   void add(std::string const& uri, Handler const& handlers);

   void print();

   template<typename T>
   void handle();

   void maddleware(std::string const& uri, std::function<StatusCode(Request*)> const& handler);
   void maddleware(METHOD method, std::string const& uri, std::function<StatusCode(Request*)> const& handler);


public:
   Handler():m_uri_tree(NULL){}
   Handler(Handler const& other):Handler(){*this=other;}
   Handler(Handler && other):Handler(){*this=std::move(other);}
   Handler& operator = (Handler const& other){copy(other.m_uri_tree); return *this;}
   Handler& operator = (Handler && other){if(other.m_uri_tree!=m_uri_tree){m_uri_tree=other.m_uri_tree; other.m_uri_tree=NULL;}return *this;}
   ~Handler(){del(m_uri_tree);m_uri_tree=NULL;}


   void filter(METHOD m, std::string const& vpath, 
               std::vector<std::function<StatusCode(Request*)>> &maddlewares, 
               std::vector<std::function<StatusCode(Request*)>> &handlers);
private:

   URITree* build(METHOD, std::string const&, std::function<StatusCode(Request*)> const&, bool, bool onlypath=false);
   void del(URITree *p);
   void copy(URITree *s);

   void filter(METHOD m, URITree* p,
               std::vector<std::string> vchildren, 
               std::vector<std::function<StatusCode(Request*)>> &maddlewares, 
               std::vector<std::function<StatusCode(Request*)>> &handlers);

    URITree* m_uri_tree;
    friend XXS_Http_Server;
};
//register maddleware
//register service
//{
//method: get, del, post
//path:
//handler(request)
//}

//handler(request)


class XXS_Http_Server {
public:

    XXS_Http_Server(int port, std::string const& view_root_path="", int maxWorkers=100):
     m_listen_port(port),m_fileChache(view_root_path),
     m_httpReqeustPool(3,maxWorkers){init(port);}

    ~XXS_Http_Server(){}

    void registerHandler(std::string const& root_uri, Handler const& handler);

    void Run();
 
private:
    void init(int port);

    
// config // accep file path
// init
// register http::Handler reference
// workers <- http::Request

// listen
// epoll  event accept, read, write

//accept -> Request -> (read epoll) | -> workers
//Request.recv -> handlers -> Request.send ->(close, read epoll)

private:
    int m_epoll_fd;
    int m_listen_fd;
    int m_listen_port;

    FileCache           m_fileChache;
    Handler             m_handler;
    HttpRequestWorker   m_httpReqeustPool;

    friend Request;
};


}






#endif