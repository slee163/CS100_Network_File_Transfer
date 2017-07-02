#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/sendfile.h>
#include <string.h>
#include <pthread.h>
#include <queue>

#define MAX_BACKLOG 200
#define THREAD_PORT 18681
#define MAX_THREADS 5

using namespace std;

struct task
{
    int client_fd;
    string file;
};

int server_fd;
pthread_t threads[MAX_THREADS];
queue<task> taskQueue;

pthread_mutex_t queue_mutex1;
bool serverShutDown = false;

void errcheck(int result, string msg, bool fatal)
{
    if(result == -1)
    {
        perror(msg.c_str());
        if(fatal)
        {
            pthread_exit(NULL);
        }
    }
}

void setupSockaddr(sockaddr_in& serv_address, sockaddr_in& clin_address)
{
    memset(&serv_address,0,sizeof(serv_address));
    bzero(&serv_address,sizeof(serv_address));
    memset(&clin_address,0,sizeof(clin_address));
    bzero(&clin_address,sizeof(clin_address));
    
    serv_address.sin_family = AF_INET;
    serv_address.sin_port = htons(THREAD_PORT);
    serv_address.sin_addr.s_addr = INADDR_ANY;
}

void socketSetup(sockaddr_in& sock_address)
{
    server_fd = socket(AF_INET,SOCK_STREAM,0);
    errcheck(server_fd,"Error creating socket",true);
    
    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET,SO_REUSEADDR,&optval, sizeof(optval));
    
    errcheck(bind(server_fd,(struct sockaddr *) &sock_address, sizeof(sock_address)), "Error binding server",true);
    errcheck(listen(server_fd,MAX_BACKLOG), "Error establishing listener",true); 
}

int getClientInfo(int server_fd, sockaddr_in& sock_address)
{
    int client_fd;    
    socklen_t clin_length = sizeof(sock_address);

    client_fd = accept(server_fd, (struct sockaddr *) &sock_address, &clin_length);
    errcheck(client_fd, "Error accepting connection",false);
    
    return client_fd;
}

int getBasePath(string path)
{
    int split = path.find_last_of("/");
    return (split == path.npos) ? 0:(split + 1);
}

void copyFile(int clin_fd, string path)
{   
    int base_start = getBasePath(path);
    
    int file_fd;
    file_fd = open(path.c_str(),O_RDONLY);
    
    //tells it to skip files with permission denied
    if(file_fd == -1)
    {
    	string err = "Error opening file" + path;
    	char errormsg[256];
    	strcpy(errormsg,err.c_str());
        if(errno == EACCES)
        {return;}
        else
        {errcheck(-1,errormsg,true);}
    }
        
    struct stat file_info;
    errcheck(stat(path.c_str(), &file_info),"Error acquiring stats", true);
    
    string mod_path = path.substr(base_start);
    size_t name_size = mod_path.size() + 1;
    const mode_t permissions = file_info.st_mode;
    const off_t file_size = file_info.st_size;
    
    //this section sends over specifications, size of name, name, and permissions for proper copy
    errcheck(write(clin_fd, &name_size, sizeof(name_size)),"Error copying file", true);
    errcheck(write(clin_fd, mod_path.c_str(), name_size),"Error copying file", true);
    errcheck(write(clin_fd, &permissions, sizeof(permissions)),"Error copying file", true);
    errcheck(write(clin_fd, &file_size, sizeof(file_size)),"Error copying file", true);
    
    //sends the file data (contents)
    
    errcheck(sendfile(clin_fd, file_fd, NULL, file_info.st_size), "Error sending file", true);
    errcheck(close(file_fd),"Error closing file", true);
}



string getDataTargets(int clin_fd)
{
    vector<pthread_t> threads;
    char buffer[BUFSIZ];
    memset(&buffer,0,sizeof(buffer));
    size_t len;
    
    read(clin_fd,&len,sizeof(len));
    if(read(clin_fd,&buffer,len) <= 0)
    {
        cout << "Error reading from socket";
        exit(-1);
    }
    return string(buffer);
}

task getNextTask()
{
    bool tasksToDo = true;
    while(!serverShutDown)
    {
        tasksToDo = true;
        task nextRequest;
        pthread_mutex_lock(&queue_mutex1);
        {
            if(taskQueue.empty())
            {tasksToDo = false;}
            else
            {
                nextRequest = taskQueue.front();
                taskQueue.pop();
            }
        }
        pthread_mutex_unlock(&queue_mutex1);
        if(!tasksToDo)
        {continue;}
        return nextRequest;
    }
    pthread_exit(NULL);
    
}

void* processRequest(void* v)
{
    while(!serverShutDown)
    {
        task request = getNextTask();
        copyFile(request.client_fd,request.file);
        errcheck(shutdown(request.client_fd,SHUT_WR),"Error sending eof", true);
        errcheck(close(request.client_fd), "Error closing client socket", true);
    }
    pthread_exit(NULL);
}

void runServer(sockaddr_in& clin_address)
{
    int client_fd;
    pid_t pid;
    for(;;)
    {
        client_fd = getClientInfo(server_fd,clin_address);
        string file = getDataTargets(client_fd);
        task taskRequest;
        taskRequest.client_fd = client_fd;
        taskRequest.file = file;
        taskQueue.push(taskRequest);
    }
}


void closeServer(int signum)
{
    cout << "tprServer is now closing" << endl;
    errcheck(close(server_fd), "Error closing server socket", true);
    cout << "Joining threads" << endl;
    serverShutDown = true;
    for(int i = 0; i < MAX_THREADS; ++i)
    {pthread_join(threads[i],NULL);}
    pthread_mutex_destroy(&queue_mutex1);
    cout << "poolServer has exited" << endl;
    pthread_exit(NULL);
}


int main()
{
    char host_name[256];
    struct sockaddr_in serv_address;
    struct sockaddr_in clin_address;
    setupSockaddr(serv_address, clin_address);

    signal(SIGINT,closeServer);
    pthread_mutex_init(&queue_mutex1, NULL);
    
    for(int i = 0; i < MAX_THREADS; ++i)
    {
        if(pthread_create(&threads[i],NULL,processRequest,(void*) &i) < 0)
        {
            perror("Error creating thread");
            exit(-1);
        }
    }
    
    socketSetup(serv_address);
    errcheck(gethostname(host_name,sizeof(host_name)),"Error getting host name", true);
    cout << "poolServer is now active" << endl;
    cout << "host name: " << host_name << endl;
    cout << "port number: " << THREAD_PORT << endl;
    cout << "max backlog: " << MAX_BACKLOG << endl;
    cout << "exit with (ctrl-C)" << endl; 
    runServer(clin_address);
    
    return 0;
}
