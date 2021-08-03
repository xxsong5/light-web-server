#include "httplightserver.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <unordered_map>
#include <algorithm>

namespace http
{
static std::vector<std::string> readFiles(std::string const& path,
                                            std::function<bool(std::string const&filename)> unary)
{
    std::vector<std::string> vfiles;

    if (path.empty()) {
        return vfiles;
    }

    DIR *dir = opendir(path.c_str());
    if (dir == NULL) {
        return vfiles;
    }

    std::string filePath = path;
    if (filePath.c_str()[filePath.length()-1] != '/')
        filePath += "/";

    dirent *d_ent;
    while(( d_ent = readdir(dir)) != NULL) {

        if ((d_ent->d_type & DT_REG) && unary(d_ent->d_name)) {
                vfiles.push_back(filePath+d_ent->d_name);
        }
        else if (d_ent->d_type & DT_DIR) {
            if (strcmp(d_ent->d_name, ".") != 0 && strcmp(d_ent->d_name, "..") != 0)
            {
                auto v = readFiles(filePath+d_ent->d_name, unary);
                vfiles.insert(vfiles.end(), v.begin(), v.end());
            }
        }
    }
    closedir(dir);
    return vfiles;
}

bool Request::donext() 
{
    if (m_handlers.size() <= m_currentIdx) {
        ::close(this->m_fd);
        delete this;
        return false;
    }
    StatusCode code = m_handlers[m_currentIdx](this);

    if((code == XXS_CANNEL||code == XXS_ERROR) && m_currentIdx < m_handlers.size()-1) {
        m_currentIdx = m_handlers.size()-1;
    }

    if(code == XXS_OK) {
        ++m_currentIdx;
    }

    return code != XXS_END;
}

StatusCode Request::files(Request* rq)
{
    printf("getfile\n");

    const XXS_FILE* f = m_pxxs_http_server->m_fileChache.get(m_path);
    if (f) {

        //if (f->date == m_if_modified_since) {
        //    writeCode(XXS_CODE_Content_Not_Change);
        //    return StatusCode::XXS_CANNEL;
        //}

        if (m_method == XXS_METHOD_HEAD || (m_method == XXS_METHOD_GET && m_range.empty())) {
            if (f->len > 10240) {
                writeHead("Accept-Ranges", "bytes");
            }
            writeHead("Allow", "GET,HEAD");
            //writeHead("Content-Language", "en,zh");
            writeHead("Content-Type", f->ftype);
            writeHead("Etag", f->etag);
            writeHead("Last-Modified", f->date);

            if (m_method == XXS_METHOD_GET) {
                if (f->expires_seconds) {
                    writeHead("Cache-Control", "max-age="+std::to_string(f->expires_seconds*2));
                    //writeHead("Cache-Control", "no-cache");
                    time_t curtime; time(&curtime);curtime += (time_t)f->expires_seconds;
                    std::string exp = ctime(&curtime);exp.pop_back();
                    writeHead("Expires", exp);
                }
                write(std::string(f->p, f->len));
            }

            writeCode(XXS_CODE_OK);
            return StatusCode::XXS_CANNEL;
        }

        if (m_method == XXS_METHOD_GET && !m_range.empty()) {

            if (memcmp(m_range.c_str(), "bytes=", 6) == 0) {
                size_t d = m_range.find('-', 6);
                if (d != std::string::npos) {
                    size_t d1 = atoi(m_range.substr(6, d-6).c_str());
                    size_t d2 = atoi(m_range.substr(d+1).c_str());
                    if (d1 < d2 && d2 < f->len) {
                        writeHead("Content-Type", f->ftype);
                        //writeHead("Content-Length", std::to_string(d2-d1+1));
                        writeHead("Accept-Ranges", "bytes");
                        writeHead("Etag", f->etag);
                        writeHead("Last-Modified", f->date);
                        writeHead("Content-Range", "bytes " + m_range.substr(6) + "/"+ std::to_string(f->len));
                        write(std::string(f->p+d1, d2-d1+1));
                        writeCode(XXS_CODE_Partial_Content);
                        return StatusCode::XXS_CANNEL;
                    }
                }
            }

            writeCode(XXS_CODE_Bad_Request);
            return StatusCode::XXS_CANNEL;
        }
    }

    return StatusCode::XXS_OK;
}

/*
StatusCode Request::send(Request* rq)
{
        if (m_httpResponseStatus.empty()) {
            writeCode(m_httpResponse.empty() ? XXS_CODE_Forbidden : XXS_CODE_OK);
        }

        m_httpResponseStatus += "\r\n";
        for (auto d : m_httpResponseHeaders) {
            m_httpResponseStatus += d + "\r\n";
        }


        m_httpResponse = m_httpResponseStatus + "\r\n" + m_httpResponse;

        std::cout << "send: " << m_httpResponse << std::endl;

        ::send(m_fd, m_httpResponse.c_str(), m_httpResponse.length(), 0);

        //this->selfdestroy();
        return XXS_END; 
}
*/

StatusCode Request::send(Request* rq)
{
    // collect response
    if (m_temporarydata[1] == 0) {
        //first in
        if (m_httpResponseStatus.empty()) {
            writeCode(m_httpResponse.empty() ? XXS_CODE_Forbidden : XXS_CODE_OK);
        }

        //if (m_keepAlive || m_connection=="keep-alive") {
        //    writeHead("Connection", "keep-alive");
        //}

        m_httpResponseStatus += "\r\n";
        for (auto d : m_httpResponseHeaders) {
            m_httpResponseStatus += d + "\r\n";
        }
        m_httpResponseStatus += "\r\n";

        m_temporarydata[1] = 1;

        printf("send header: %s\n", m_httpResponseStatus.c_str());
    }


    // patched send
    int slen=0, len = m_httpResponseStatus.length();
    if (len && (slen=::send(m_fd, m_httpResponseStatus.c_str(), len, 0)) <= 0 ) {
        if((errno==EINTR || errno==EAGAIN)) {
            return StatusCode::XXS_AGINE;
        }
        //unexpected end
        this->selfdestroy();
        return StatusCode::XXS_END;
    }
    if (slen > 0) {
        m_httpResponseStatus.erase(0, slen);
        if (slen != len) {
            return StatusCode::XXS_AGINE;
        }
    }

    slen=0, len = m_httpResponse.length();
    if (len && (slen=::send(m_fd, m_httpResponse.c_str(), std::min(4096,len), 0)) <= 0 ) {
        if((errno==EINTR || errno==EAGAIN)) {
            return StatusCode::XXS_AGINE;
        }
        //unexpected end
        this->selfdestroy();
        return StatusCode::XXS_END;
    }
    if (slen > 0) {
        m_httpResponse.erase(0, slen);
        if (slen != len) {
            return StatusCode::XXS_AGINE;
        }
    }

    // disable for now
    // clear & reepoll if keepalive
    //if (m_keepAlive || m_connection=="keep-alive") {
    //    std::cout << "epoll reepolled fd: " << m_fd << std::endl;
    //    struct epoll_event ev;
    //    memset(&ev, 0, sizeof(epoll_event));
    //    ev.events = EPOLLIN | EPOLLONESHOT;
    //    ev.data.ptr = this;
    //    this->clear();
    //    if (epoll_ctl(m_pxxs_http_server->m_epoll_fd, EPOLL_CTL_MOD, m_fd, &ev) == 0) {
    //        return StatusCode::XXS_END;
    //    }
    //}
    
    this->selfdestroy();
    return StatusCode::XXS_END;
}


bool Request::close(){ std::cout << "epoll closed fd: " << m_fd << std::endl; ::close(m_fd);return true;}
void Request::selfdestroy(){this->close(); delete this;}
void Request::initHandlers()
{
    m_handlers.clear();
    m_handlers.push_back(std::bind(&Request::recv, this, std::placeholders::_1));
    m_handlers.push_back(std::bind(&Request::parse, this, std::placeholders::_1));
    m_handlers.push_back(std::bind(&Request::handle, this, std::placeholders::_1));
}
void Request::clear()
{
    m_keepAlive  = false;
    m_currentIdx = 0;
    initHandlers();
    m_httpContentIdx = 0;
    m_httpContentLen = 0;
    m_method = METHOD::XXS_METHOD_UNKNOWN;
    m_path.clear();
    m_range.clear();
    m_connection.clear();
    m_contentType.clear();
    m_cacheControl.clear();
    m_if_modified_since.clear();
    m_upgrade_insecure_requests.clear();
    m_httpRequest.clear();
    m_httpResponse.clear();
    m_httpResponseStatus.clear();
    m_httpResponseHeaders.clear();
    memset(m_temporarydata, 0, 8);
}

StatusCode Request::recv(Request* rq)
{
    StatusCode status = StatusCode::XXS_OK;

    constexpr int buflen = 4096;
    m_httpRequest.resize(m_httpRequest.length()+buflen, 0);
    char *buf = &m_httpRequest[m_httpRequest.length()-buflen];

    int rcvlen = ::recv(m_fd, buf, buflen, 0);
    if((rcvlen < 0 && (errno==EINTR||errno==EAGAIN)) || (rcvlen == buflen))
    {
        status = ((rcvlen > 0 )||(rcvlen < 0 && m_temporarydata[0]++ < 3)) ? XXS_AGINE : XXS_OK;
        m_temporarydata[0] = (status==XXS_OK || rcvlen>0) ? 0 : m_temporarydata[0];
        rcvlen = (rcvlen < 0)? 0 : rcvlen;
    }
    else if(rcvlen <= 0)
    {
        rcvlen = 0;
    }
    m_httpRequest.resize(m_httpRequest.length()-buflen+rcvlen,0);

    if(m_httpRequest.empty()) {
        this->selfdestroy();
        return StatusCode::XXS_END;
    }

//    std::cout << "epoll recv: " << m_fd << m_httpRequest << std::endl;

    return status;
}




StatusCode Request::parse(Request* rq)
{
    printf("parse\n");

    m_method = XXS_METHOD_UNKNOWN;
    if (m_httpRequest.length() < 5) {
        return XXS_ERROR;
    }

   static const std::unordered_map<std::string, std::function<void(Request*, std::string const&)>> http_head_setter = {
        {"Host:",           [](Request* rq, std::string const& val){rq->m_host=val;}},
        {"User-Agent:",     [](Request* rq, std::string const& val){rq->m_agent=val;}},
        {"Accept:",         [](Request* rq, std::string const& val){rq->m_accept=val;}},
        {"Range:",          [](Request* rq, std::string const& val){rq->m_range=val;}},
        {"Connection:",     [](Request* rq, std::string const& val){rq->m_connection=val;}},
        {"Cookie:",         [](Request* rq, std::string const& val){rq->m_cookie=val;}},
        {"Content-Length:", [](Request* rq, std::string const& val){rq->m_httpContentLen=atol(val.c_str());}},
        {"Content-Type:",   [](Request* rq, std::string const& val){rq->m_contentType=val;}},
        {"Cache-Control:",  [](Request* rq, std::string const& val){rq->m_cacheControl=val;}},
        {"sec-ch-ua:",      [](Request* rq, std::string const& val){rq->m_sec_ch_ua=val;}},
        {"If-Modified-Since:",[](Request* rq, std::string const& val){rq->m_if_modified_since=val;}},
        {"sec-ch-ua-mobile:", [](Request* rq, std::string const& val){rq->m_sec_ch_ua_mobile=val;}},
        {"Upgrade-Insecure-Requests:", [](Request* rq, std::string const& val){rq->m_upgrade_insecure_requests=val;}},
        {"Sec-Fetch-Site:", [](Request* rq, std::string const& val){rq->m_sec_fetch_site=val;}},
        {"Sec-Fetch-Mode:", [](Request* rq, std::string const& val){rq->m_sec_fetch_mode=val;}},
        {"Sec-Fetch-User:", [](Request* rq, std::string const& val){rq->m_sec_fetch_user=val;}},
        {"Sec-Fetch-Dest:", [](Request* rq, std::string const& val){rq->m_sec_fetch_dest=val;}},
        {"Accept-Encoding:",[](Request* rq, std::string const& val){rq->m_accept_encoding=val;}},
        {"Accept-Language:",[](Request* rq, std::string const& val){rq->m_accept_language=val;}},
   };


    char c = 0, cc = 0;
    size_t llpos = 0, lpos = 0, pos = 0, lc= 0, len = m_httpRequest.length();
    while(pos < len) {

        char w = m_httpRequest[pos];

        if (w == ' ' && c == ' ') {
            ++pos;
            continue;
        }
        else if (w == '\n' && c == '\r' && cc == '\n') {
            m_httpContentIdx = pos+1;
            break;
        }
        else if ((w==' '&& lc==0)||(w==' ' && c==':' && lc>0)) {
            llpos = lpos;
            lpos = pos;
        }
        else if (w == '\n' && c == '\r') {
            if (lc == 0) {
                //first line
                if (memcmp("GET", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_GET;
                }
                else if (memcmp("POST", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_POST;
                }
                else if (memcmp("HEAD", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_HEAD;
                }
                else if (memcmp("PUT", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_PUT;
                }
                else if (memcmp("CONNECT", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_CONNECT;
                }
                else if (memcmp("DELETE", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_DELETE;
                }
                else if (memcmp("TRACE", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_TRACE;
                }
                else if (memcmp("OPTIONS", &m_httpRequest[0], llpos) == 0) {
                    m_method = XXS_METHOD_OPTIONS;
                }

                m_path = m_httpRequest.substr(llpos+1, lpos-llpos-1);
                m_version= m_httpRequest.substr(lpos+1, pos-lpos-2);
            }
            if (lc != 0) {
                //other lines
                std::string key = m_httpRequest.substr(llpos+1, lpos-llpos-1);
                std::string val = m_httpRequest.substr(lpos+1, pos-lpos-2);

                auto iter = http_head_setter.find(key);
                if (iter != http_head_setter.end()) {
                    iter->second(rq,val);
                }
                //std::cout << "key:"<< " " << key<< std::endl;
                //std::cout << "val:"<< " " << val<< std::endl;
            }

            ++lc;
            llpos = pos;
            lpos = pos;
        }

        cc = c;
        c  = w;
        ++pos;
    }

    return StatusCode::XXS_OK;
}


StatusCode Request::handle(Request* rq)
{
    printf("handle %s\n", m_path.c_str());

    std::vector<RequestHandler> maddlewares, handlers;
    m_pxxs_http_server->m_handler.filter(m_method, m_path, maddlewares, handlers);

    // insert maddlewares
    m_handlers.insert(m_handlers.end(), maddlewares.begin(), maddlewares.end());

    // insert fileservice 
    m_handlers.push_back(std::bind(&Request::files, this, std::placeholders::_1));

    // insert main handlers 
    m_handlers.insert(m_handlers.end(), handlers.begin(), handlers.end());

    // insert end
    m_handlers.push_back(std::bind(&Request::send, this, std::placeholders::_1));

    //std::cout << "m_path" << ": " << m_path << std::endl;
    //std::cout << "m_version" << ": " << m_version<< std::endl;
    //std::cout << "m_host" << ": " << m_host<< std::endl;
    //std::cout << "m_agent" << ": " << m_agent<< std::endl;
    //std::cout << "m_cookie" << ": " << m_cookie<< std::endl;
    //std::cout << "m_accept" << ": " << m_accept<< std::endl;
    //std::cout << "m_accept_encoding" << ": " << m_accept_encoding<< std::endl;
    //std::cout << "m_accept_language" << ": " <<m_accept_language << std::endl;
    //std::cout << "m_connection" << ": " << m_connection<< std::endl;
    //std::cout << "m_contentType" << ": " << m_contentType<< std::endl;
    //std::cout << "m_cacheControl" << ": " <<m_cacheControl<< std::endl;
    //std::cout << "m_upgrade_insecure_requests" << ": " << m_upgrade_insecure_requests<< std::endl;
    //std::cout << "m_sec_ch_ua" << ": " << m_sec_ch_ua<< std::endl;
    //std::cout << "m_sec_ch_ua_mobile" << ": " << m_sec_ch_ua_mobile<< std::endl;
    //std::cout << "m_sec_fetch_site" << ": " << m_sec_fetch_site<< std::endl;
    //std::cout << "m_sec_fetch_mode" << ": " << m_sec_fetch_mode<< std::endl;
    //std::cout << "m_sec_fetch_user" << ": " << m_sec_fetch_user<< std::endl;
    //std::cout << "m_sec_fetch_dest" << ": " << m_sec_fetch_dest<< std::endl;


    return StatusCode::XXS_OK;
}

void Handler::handle(std::string const& uri, std::function<StatusCode(Request*)> const& hander)
{
    return Handler::handle(METHOD::XXS_METHOD_UNKNOWN, uri, hander);
}

void Handler::maddleware(std::string const& uri, std::function<StatusCode(Request*)> const& hander)
{
    return Handler::maddleware(METHOD::XXS_METHOD_UNKNOWN, uri, hander);
}

void Handler::handle(METHOD method, std::string const& uri, std::function<StatusCode(Request*)> const& hander)
{
    build(method, uri, hander, false);
    return; 
}

void Handler::maddleware(METHOD method, std::string const& uri, std::function<StatusCode(Request*)> const& hander)
{
    build(method, uri, hander, true);
    return; 
}

URITree* Handler::build(METHOD method, 
                    std::string const& uri, 
                    std::function<StatusCode(Request*)> const& handler, 
                    bool maddleware, bool onlypath)
{
    if (uri.empty()) {
        return m_uri_tree;
    }

    if (m_uri_tree == NULL) {
        m_uri_tree = new URITree;
        m_uri_tree->name = "/";
        m_uri_tree->parent= NULL;
    }

    if (uri == "/" || uri == "//") {
        if (!onlypath) {
            if (!maddleware) {
                m_uri_tree->vhanders.emplace_back(std::pair<METHOD, std::function<StatusCode(Request*)>>{method,handler});
            }else{
                m_uri_tree->vhijacks.emplace_back(std::pair<METHOD, std::function<StatusCode(Request*)>>{method,handler});
            }
        }
        return m_uri_tree;
    }

    char b = 0;
    size_t pos = 0, d = 0, len = uri.length();
    URITree *pnode = m_uri_tree;
    std::string name;
    while(pos < len) {
        char w = uri.at(pos);
        if (w == '/' && (b == '/' || pos == 0)) {
            b = w;
            pos += 1;
            continue;
        }

        if (w != '/') {
            name.push_back(w);
        }

        if ((w == '/' || pos == (len-1)) && !name.empty()) {
            //build
            bool has = false;
            for(size_t i=0,e=pnode->vchild.size(); i<e; i++) {
                if (pnode->vchild[i]->name == name) {
                    has = true;
                    pnode = pnode->vchild[i];
                    break;
                }
            }
            //std::cout << name << std::endl;
            if (!has) {
                URITree* p = new URITree;
                p->name = name;
                p->parent = pnode;
                pnode->vchild.push_back(p);
                pnode = p;
            }

            if (pos == (len-1)) {
                if (!onlypath) {
                    if (!maddleware) {
                        pnode->vhanders.emplace_back(std::pair<METHOD, std::function<StatusCode(Request*)>>{method,handler});
                    }else{
                        pnode->vhijacks.emplace_back(std::pair<METHOD, std::function<StatusCode(Request*)>>{method,handler});
                    }
                }
            }
            name.clear();
        }

        b = w;
        pos += 1;
    }

    return pnode;
}


void Handler::del(URITree *p)
{
    if (!p) {
        return;
    }
    for (int i = 0, e = p->vchild.size(); i < e; i++) {
        del(p->vchild[i]);
    }
    delete p;
}

void copy(URITree *d, URITree *s)
{
    if (d == NULL || s == NULL) {
        return;
    }

    d->name = s->name;
    for (auto m : s->vhanders) {
        d->vhanders.push_back(m);
    }
    for (auto m : s->vhijacks) {
        d->vhijacks.push_back(m);
    }

    for (size_t i=0, e= s->vchild.size(); i < e; i++) {

        URITree * c = s->vchild[i];
        URITree * p = NULL;
        auto iter = std::find_if(d->vchild.begin(), d->vchild.end(), [c](URITree* node){
                                                                        return node->name == c->name;});
        if (iter == d->vchild.end()) {
            p = new URITree;
            p->parent = d;
            d->vchild.push_back(p);
        }else{
            p = *iter;
        }
        http::copy(p, c);
    }
}


void Handler::filter(METHOD m, std::string const& path, 
                    std::vector<std::function<StatusCode(Request*)>> &maddlewares, 
                    std::vector<std::function<StatusCode(Request*)>> &handlers)
{
    if (path.empty() || path == "/") {
        for (auto const& pair : m_uri_tree->vhijacks) {
            if (pair.first == METHOD::XXS_METHOD_UNKNOWN || pair.first == m) {
                maddlewares.push_back(pair.second);
            }
        }

        for (auto const& pair : m_uri_tree->vhanders) {
            if (pair.first == METHOD::XXS_METHOD_UNKNOWN || pair.first == m) {
                handlers.push_back(pair.second);
            }
        }
        
        return;
    }

    std::string name;
    std::vector<std::string> vpath;
    for(size_t i = 0, e = path.length(); i < e; i++) {
        char d = path[i];
        if (d != '/' || d != '?') {
            name.push_back(d);
        }
        if ((d=='/'|| d == '?' || (i+1)==e) && !name.empty()) {
            vpath.push_back(std::move(name));
        }
        if (d == '?') {
            break;
        }
    }

    std::cout << "fliter path: ";
    for (auto a : vpath) {
        std::cout << a << " ";
    }
    std::cout  << std::endl;



    return filter(m, m_uri_tree, vpath, maddlewares, handlers);
}


void Handler::filter(METHOD m, 
                    URITree* p, 
                    std::vector<std::string> vchildren, 
                    std::vector<std::function<StatusCode(Request*)>> &maddlewares, 
                    std::vector<std::function<StatusCode(Request*)>> &handlers)
{
    if (p == NULL) {
        return;
    }

    for (auto const&pair : p->vhijacks) {
        if (pair.first == METHOD::XXS_METHOD_UNKNOWN || pair.first == m) {
            maddlewares.push_back(pair.second);
        }
    }

    if (!vchildren.empty()) {
        auto iter = std::find_if(p->vchild.begin(), p->vchild.end(), [&vchildren](URITree* e){
                return e->name == vchildren[0];});
        if (iter != p->vchild.end()) {
            filter(m, *iter, std::vector<std::string>(vchildren.begin()+1, vchildren.end()), maddlewares, handlers);
        }
    }

    if (handlers.empty()) {
        for (auto const&pair : p->vhanders) {
            if (pair.first == METHOD::XXS_METHOD_UNKNOWN || pair.first == m) {
                handlers.push_back(pair.second);
            }
        }
    }
}

void Handler::add(std::string const& uri, Handler const& handlers)
{
    if (this->m_uri_tree == handlers.m_uri_tree) {
        if (uri.empty() || uri == "/" || uri == "//") {
            return;
        }
        Handler tmp = handlers;

        URITree *node = build(METHOD::XXS_METHOD_UNKNOWN, 
                              uri, [](Request*){return StatusCode::XXS_OK;}, false, true);
        if (node == NULL) {
            return;
        }

        std::string name = node->name;
        http::copy(node, tmp.m_uri_tree);
        node->name = name;

        return;
    }

    URITree *node = build(METHOD::XXS_METHOD_UNKNOWN, 
                          uri, [](Request*){return StatusCode::XXS_OK;}, false, true);
    if (node == NULL) {
        return;
    }

    std::string name = node->name;
    http::copy(node, handlers.m_uri_tree);
    node->name = name;
}

void print(URITree *p, std::vector<std::string> &v)
{
    if (p == NULL) {
        return; 
    }
    v.push_back(p->name);

    if (p->vhanders.size() > 0) {
        std::string s = v[0];
        for(size_t i=1, e=v.size(); i < e; i++) {
            s+= v[i] + "/";
        }
        std::cout << p->vhanders.size() << "\t" << s << std::endl;
    }
    for (auto c : p->vchild) {
        print(c, v);
    }
    v.pop_back();
    return;
}

void Handler::print()
{
    std::cout << "handlers vs uri-path: " << std::endl;
    std::vector<std::string> v;
    http::print(m_uri_tree, v);
    return;
}


void Handler::copy(URITree *s)
{
    if (m_uri_tree == s) {
        return;
    }

    del(m_uri_tree);
    if (s == NULL) {
        m_uri_tree = NULL;
        return;
    }

    m_uri_tree = new URITree;
    m_uri_tree->parent = NULL;

    http::copy(m_uri_tree, s);
}


void HttpRequestWorker::work(int id)
{
    printf("worker [%d] is started\n", id);
    while(isstopped(id)) {
        std::vector<Request*> vreq = fetchTasks(1, id);

        while(vreq.size()) {
            for (int i = vreq.size()-1; i >= 0; i--) {
                if (!vreq[i]->donext()) {
                    vreq.erase(vreq.begin()+i);
                }
            }
        }
    }
    printf("worker [%d] is stopped\n", id);
    return;
}

void HttpRequestWorker::conductor()
{
    printf("conductor started\n");
    //start regulars
    for (int i=0; i < m_regularWorker;i++) {
        start(m_workers.size());
        m_workers.push_back(std::thread(&HttpRequestWorker::work, this, m_workers.size()));
    }

    while(isstopped(0)) {
        //check task
        size_t ctasks = leftTasks();

        //add worker
        if (ctasks > 10000 && m_workers.size() < m_maxWorker) {
            for (int i=0, e=std::min<int>(m_maxWorker-m_workers.size(), 10); i < e;i++) {
                start(m_workers.size());
                m_workers.push_back(std::thread(&HttpRequestWorker::work, this, m_workers.size()));
            }
        }
        else if (ctasks > 1000 && m_workers.size() < m_maxWorker) {
            start(m_workers.size());
            m_workers.push_back(std::thread(&HttpRequestWorker::work, this, m_workers.size()));
        }

        //remove laizi worker
        if (ctasks < 100 && m_workers.size() > m_regularWorker+1) {
            size_t last = (m_workers.size()-1);
            stop(last,last);
            if (m_workers[last].joinable()) {
                m_workers[last].join();
            }
            m_workers.pop_back();
        }

        std::this_thread::sleep_for(std::chrono::microseconds(1000000));
    }

    //stoped left
    stop(0, -1);
    for (int i = 1, e = m_workers.size(); i < e; i++) {
        if (m_workers[i].joinable()) {
            m_workers[i].join();
        }
    }
    printf("conductor stopped\n");
}

void HttpRequestWorker::start(int id)
{
    std::lock_guard<std::mutex> lock(m_workerMutex);
    m_workerFlag[id] = true;
    return;
}

bool HttpRequestWorker::isstopped(int id)
{
    std::lock_guard<std::mutex> lock(m_workerMutex);
    return m_workerFlag[id];
}

void HttpRequestWorker::stop(int fid, int lid)
{
    std::lock_guard<std::mutex> lock(m_workerMutex);

    if (lid < 0) {
        lid = m_workerFlag.size()-1;
    }

    for (int i = fid, e = std::min<int>(lid, m_workerFlag.size()-1); i <= e; i++) {
        m_workerFlag[i]=false;
    }
}

size_t HttpRequestWorker::leftTasks()
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    return m_tasks.size();
}

void HttpRequestWorker::pushTask(Request* rq)
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    m_tasks.push_back(rq);
    m_taskSync.notify_one();
}

std::vector<Request*> HttpRequestWorker::fetchTasks(size_t max, int workid, int timeout)
{
    std::vector<Request*>  rqs;
    std::unique_lock<std::mutex> lock(m_taskMutex);
    if (m_tasks.size() == 0) {
        m_taskSync.wait_for(lock,std::chrono::seconds(timeout));
    }
    size_t c = std::min(max, m_tasks.size());
    rqs.assign(m_tasks.begin(), m_tasks.begin()+c);
    m_tasks.erase(m_tasks.begin(), m_tasks.begin()+c);

    return rqs;
}

FileCache::FileCache(std::string const& rootPath)
{
    pthread_rwlock_init(&rwlock, NULL);
    root = rootPath.empty() ? "./" : rootPath.back() == '/' ? rootPath : rootPath+"/";
    mcheckrunning = true;
//    auto vfiles = readFiles(root, [](std::string const& f){return true;});
//    files.insert(vfiles.begin(), vfiles.end());
    mth = std::thread(&FileCache::monitor, this);
}

FileCache::~FileCache()
{
    mcheckrunning = false;
    if (mth.joinable()) {
        mth.join();
    }
    pthread_rwlock_destroy(&rwlock);
}

void FileCache::monitor()
{
    while(mcheckrunning) 
    {
        pthread_rwlock_rdlock(&rwlock);
        auto cpcachefiles = cachefiles;
        pthread_rwlock_unlock(&rwlock);

        std::vector<std::string> vneedupdate;
        for (auto pair: cpcachefiles) {

            std::string fn = pair.first;
            XXS_FILE*   fp = pair.second;

            if (fp == NULL) { continue; }

            int fd = open(fn.c_str(), O_RDONLY);
            if (fd > 0) {
                struct stat sb;
                fstat(fd, &sb);
                close(fd);
                std::string date = ctime(&sb.st_mtime); date.pop_back(); //the last alph is '\n'
                if (date != fp->date || fp->age == 1) {
                    vneedupdate.push_back(fn);
                }
            }else{
                vneedupdate.push_back(fn);
            }
            fp->age = std::max<size_t>(1, fp->age-1);
        }

        pthread_rwlock_wrlock(&rwlock);
        for (auto const& f : vneedupdate) {
            printf("%s need update\n", f.c_str()); 
            auto iter = cachefiles.find(f);
            if (iter != cachefiles.end()) {
                if (iter->second->fd > 0 && iter->second->p && iter->second->len) {
                    munmap(iter->second->p, iter->second->len);
                    close(iter->second->fd);
                    delete iter->second;
                    cachefiles.erase(iter);
                }
                if (iter->second->fd < 0 && iter->second->p && iter->second->len) {
                    cachepages.sfree(iter->second->p);
                    delete iter->second;
                    cachefiles.erase(iter);
                }
            }
        }
        pthread_rwlock_unlock(&rwlock);

        if (mcheckrunning) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

XXS_FILE* FileCache::loadfile(std::string const& file)
{
    int fd = open(file.c_str(), O_RDONLY);
    if (fd > 0) {
        struct stat sb;
        fstat(fd, &sb);
        XXS_FILE *f = new XXS_FILE;
        if (sb.st_size < 4096) {
            //read all
            char *p = cachepages.smalloc_less4KB(sb.st_size);
            f->p = p;
            f->len = 0;
            f->fd = -1;
            size_t n, plen = sb.st_size;
            while(p && (n = read(fd,p, plen)) > 0) {
                p += n;
                plen -= n;
                f->len += n;
            }
            close(fd);
        }else{
            void *p = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p != MAP_FAILED) {
                f->p =(char*)p;
                f->len = sb.st_size;
                f->fd = fd;
            }
        }
        f->expires_seconds = 10; // 10 
        f->date = ctime(&sb.st_mtime);
        f->date.pop_back(); //the last alph is '\n'
        f->age = 1;

        size_t dot = file.rfind(".");
        if (dot != 0 && dot != std::string::npos) {
            std::string type = file.substr(dot);
            if (type == ".json") {
                f->ftype = "application/json";
            }
            else if (type == ".xml") {
                f->ftype = "application/xml";
            }
            else if (type == ".html") {
                f->ftype = "text/html";
            }
            else if (type == ".js") {
                f->ftype = "text/javascript";
            }
            else if (type == ".css") {
                f->ftype = "text/css";
            }
            else if (type == ".txt") {
                f->ftype = "text/plain";
            }
            else if (type == ".csv") {
                f->ftype = "text/comma-separated-values";
            }
            else if (type == ".png") {
                f->ftype = "image/jpeg";
            }
            else if (type == ".gif") {
                f->ftype = "image/gif";
            }
            else if (type == ".zip") {
                f->ftype = "application/zip";
            }
            else if (type == ".avi") {
                f->ftype = "video/x-msvideo";
            }
            else if (type == ".mpeg" || type == "mpg" || type == "mpe") {
                f->ftype = "video/mpeg";
            }
        }

        return f;
    }

    return 0;
}

const XXS_FILE* FileCache::get(std::string file)
{
    while(!file.empty() && file[0] == '/') {
        file = file.substr(1);
    }

    if (file.empty()) {
        file = "index.html";
    }

    file = root + file;

    printf("get file: %s\n", file.c_str());

    XXS_FILE* p = 0;
    pthread_rwlock_rdlock(&rwlock);
    auto iter = cachefiles.find(file);
    if (iter != cachefiles.end()) {
        p = (*iter).second;
    }
    pthread_rwlock_unlock(&rwlock);

    if (p == NULL) {
        //make new cache
        if(access(file.c_str(), F_OK|R_OK) != -1) {
            pthread_rwlock_wrlock(&rwlock);
            auto iter = cachefiles.find(file);
            if (iter != cachefiles.end()) {
                p = (*iter).second;
            } else {
                p = loadfile(file);
                cachefiles.emplace(file, p);
            }
            pthread_rwlock_unlock(&rwlock);
        }
    }

    if(p){p->age=500;}

    return p;
}



void XXS_Http_Server::registerHandler(std::string const& root_uri, Handler const& handler)
{
   m_handler.add(root_uri, handler);
}


void XXS_Http_Server::init(int port)
{
    m_listen_fd = socket(AF_INET, SOCK_STREAM, 0); 

    int opt=1;
    setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(m_listen_fd, F_SETFL, fcntl(m_listen_fd, F_GETFL,0)|O_NONBLOCK);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr("0.0.0.0");

    if (bind(m_listen_fd, (struct sockaddr*)&sin, sizeof(sin)) != 0){
        std::cerr << "bind failed" << std::endl;
        close(m_listen_fd);
        exit(0);
    }

    if (listen(m_listen_fd, 32) != 0) {
        close(m_listen_fd);
        std::cerr << "listen failed" << std::endl;
        exit(0);
    }

    m_epoll_fd = epoll_create(1000);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = new Request(m_listen_fd, this); //TODO:

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_listen_fd, &ev) < 0) {
         std::cerr << "add listenSocket to epoll failed!\n";
         exit(0);
    }

    std::cout << "listened at: " << port << std::endl;
}

void XXS_Http_Server::Run()
{
    static bool isrunning = true;

    signal(SIGINT,  [](int){isrunning=false;});
    signal(SIGKILL, [](int){isrunning=false;});

    while (isrunning) {
        struct epoll_event events[1000];
        int evn_cnt = epoll_wait(m_epoll_fd, events, 1000, 1000);

        //std::cout << "server running...\n";

        for (int i=0; i<evn_cnt; i++)
        {
            Request *rq = (Request*)events[i].data.ptr;

            if (rq && rq->socket() == m_listen_fd)
            {
                struct sockaddr_in caddr;
                socklen_t clen = sizeof(caddr);

                int fd = accept(m_listen_fd, (sockaddr *)&caddr, &clen);
                std::cout << "epoll accept: " << fd << std::endl;
                if (fd > 0) {
                    struct epoll_event ev;
                    memset(&ev, 0, sizeof(epoll_event));
                    ev.events = EPOLLIN | EPOLLONESHOT;
                    ev.data.ptr = new Request(fd, this);

                    //int revTimeout = 1000;
                    //setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO, (char *)&revTimeout,sizeof(int));
                    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL,0)|O_NONBLOCK);

                    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
                        std::cout << "epoll accept close: " << fd << std::endl;
                        close(fd);
                    }
                }
            }
            else if (events[i].events & EPOLLIN) {

                if (rq != NULL) {
                    m_httpReqeustPool.pushTask(rq);
                    //while(rq->donext()){}
                    //close(rq->socket());
                    //delete rq;
                }
            }
        }
    }
    close(m_listen_fd);
    close(m_epoll_fd);
    printf("stoped\n");
}


}


int main()
{
    http::XXS_Http_Server hserver(8081, "view/static");

    http::Handler handler;
    handler.handle("/", [](http::Request* r) {
        r->write("<b>hello world</b>");
        
        return http::XXS_OK;});

    handler.handle("/test", [](http::Request*r) {
        r->write("test");
        return http::XXS_OK;});

    handler.handle("/main/hello/world", [](http::Request*){return http::XXS_OK;});
    handler.handle("/abc/hello/def/", [](http::Request*){return http::XXS_OK;});
    handler.maddleware("/main/hello/world", [](http::Request*){return http::XXS_OK;});

    handler.handle("/main/hello/world", [](http::Request*){return http::XXS_OK;});

    handler.print();

    hserver.registerHandler("/", handler);

    hserver.Run();

    return 0;
}
