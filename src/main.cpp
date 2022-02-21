// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2019 Intel Corporation. All Rights Reserved.
#include <librealsense2/rs.hpp>
#include <iostream>
#include <iomanip>

#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <thread>
#include <mutex>
#include <string.h>
#include <cstring>      // Needed for memset
#include <sys/socket.h> // Needed for the socket functions
#include <netdb.h>      // Needed for the socket functions
#include <errno.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <atomic>

// https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
#define _USE_MATH_DEFINES
#include <cmath>

//#include "networktables/NetworkTable.h"
//#include "networktables/NetworkTableEntry.h"
//#include "networktables/NetworkTableInstance.h"

const double degrees_per_radian = 180 / 3.14159265358979323;
const double feet_per_metre = 1.0 / 12.0 / 0.0254;

// Code taken from John's NetworkVision
using namespace std;

#define ERROR -1
#define PortNo  5800

#define TimeOffset  0;  //1566846027200LL;

int64_t GetTimeMs() {
    timespec    ts;

    clock_gettime(CLOCK_REALTIME, &ts);

    return (((int64_t) ts.tv_sec * 1000) + (ts.tv_nsec / 1000000)) - TimeOffset;
}

void sleepMs(int ms) {
    struct timespec tim, tim2;

    tim.tv_sec = ms / 1000;
    tim.tv_nsec = (ms % 1000) * 1000000L;

    nanosleep(&tim , &tim2);
}



class SocketServer
{
//public:
    int m_serverSocket;
    int m_connectedSocket;
    std::thread * m_pThread = 0;
    mutex m_mutex;

    static void StartThread(SocketServer * pServer)
    {
        try
        {
            pServer->Run();
        }
        catch (...)
        {
            printf("StartThread catch\n");
        }
    }


public:
    SocketServer()
    {
        m_serverSocket      = ERROR;
        m_connectedSocket   = ERROR;
    }

    virtual void Run() = 0;

    bool listen(int port)
    {
        struct sockaddr_in serverAddr;
        int sockAddrSize = sizeof(serverAddr);
        memset(&serverAddr, 0, sockAddrSize);

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

        if ((m_serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
        {
            printf("Error creating server socket: %d", errno);
            return(false);
        }

        // Set the TCP socket so that it can be reused if it is in the wait state.
        int reuseAddr = 1;
        setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuseAddr, sizeof(reuseAddr));

        int one = 1;
        setsockopt(m_serverSocket, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

        // Bind socket to local address.
        if (bind(m_serverSocket, (struct sockaddr *)&serverAddr, sockAddrSize) == ERROR)
        {
            ::close(m_serverSocket);
            m_serverSocket = ERROR;
            printf("Could not bind server socket: %d", errno);
            return(false);
        }

        if (::listen(m_serverSocket, 1) == ERROR)
        {
            ::close(m_serverSocket);
            m_serverSocket  = ERROR;
            printf("Could not listen on server socket: %d", errno);
            return(false);
        }

        return(true);
    }

    bool accept()
    {
        if (m_serverSocket == ERROR)
        {
            return(ERROR);
        }

        struct sockaddr clientAddr;
        memset(&clientAddr, 0, sizeof(struct sockaddr));
        unsigned int clientAddrSize = sizeof(clientAddr);
        m_connectedSocket = ::accept(m_serverSocket, &clientAddr, &clientAddrSize);

        if (m_connectedSocket == ERROR)
            return false;

        return(true);
    }

    int read(void * pBuf, int size)
    {
        int socket;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            socket = m_connectedSocket;

        }

        if (socket == ERROR)
        {
            return(ERROR);
        }

        return(::read(socket, pBuf, size));
    }

    int write(void *pBuf, int size)
    {
        int socket;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            socket = m_connectedSocket;

        }

        if (socket == ERROR)
        {
            return(ERROR);
        }

        return(::write(socket, pBuf, size));
    }

    void Disconnect()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_connectedSocket != ERROR)
        {
            printf("ImageServer: Disconnect\n");

            ::shutdown(m_connectedSocket,SHUT_RDWR);
            ::close(m_connectedSocket);

            m_connectedSocket = ERROR;
        }

    }

    void StartServer()
    {
        if (!m_pThread)
        {
            m_pThread = new thread(StartThread, this);
        }
    }

    void StopServer()
    {
        delete m_pThread;
        m_pThread   = 0;
    }
};

class ImageProcessingServer : public SocketServer
{
    bool m_connected = false;
    std::thread   * m_pRecvThread = 0;
    std::thread   * m_pWatchDogThread = 0;

    static void StartRecvThread(ImageProcessingServer * pServer)
    {
        try
        {
            pServer->RunRecv();
        }
        catch (...)
        {
            printf("StartRecvThread catch\n");
        }
    }

    int kKeepAliveTimeout = 5000;

    static void StartWatchDogThread(ImageProcessingServer * pServer)
    {
        pServer->WatchDog();
    }

    void WatchDog()
    {
        printf("ImageServer: WatchDog started\n");

        while (m_connected)
        {
            sleepMs(1000);

            printf("ImageProcessingServer: WatchDog: %lld\n", m_keepAliveTime);

            if (m_keepAliveTime + kKeepAliveTimeout < GetTimeMs())
            {
                printf("WatchDog timeout\n");

                m_connected = false;

                Disconnect();
            }
        }

        printf("ImageServer: WatchDog ended\n");

    }

    void StartThreads()
    {
        m_pRecvThread = new thread(StartRecvThread, this);
        m_pWatchDogThread = new thread(StartWatchDogThread, this);
    }


public:
    void StopServerx()
    {
        delete m_pRecvThread;
        m_pRecvThread   = 0;

        SocketServer::StopServer();
    }

    void PingResponse()
    {
        printf("PingResponse\n");

        write((void *) "p\n", 2);
    }

    int64_t T1;
    int64_t T1P;
    int64_t T2;
    int64_t T2P;
    int64_t syncTimeOffset = 0;

    void SyncCommand(char * pArg)
    {
//      printf("SyncCommand: %s\n", pArg);

        if (pArg[0] == '1')
        {
            if (sscanf(pArg+1, "%Ld", &T1) == 1)
            {
                T2 =
                T1P = GetTimeMs();

                write((void *) "T\n", 2);
            }
            else
            {
                printf("ProcessSync: Invalid time string\n");
            }
        }
        else
        {
            if (sscanf(pArg+1, "%Ld", &T2P) == 1)
            {
                syncTimeOffset = (T1P - T1 + T2 - T2P) / 2;

                printf("ProcessSync: offset = %Ld\n", syncTimeOffset);
            }
            else
            {
                printf("ProcessSync: Invalid time string\n");
            }
        }
    }

#define kKeepAliveTimeout   5000

    int64_t m_keepAliveTime = 0;

    void RunRecv()
    {
        printf("ImageProcessingServer receiver started\n");

        while (m_connected)
        {
            int  len;
            char * p;
            char command[512];

            if ((len = read(command, sizeof(command) - 1)) == ERROR)
            {
                break;
            }

            if (len > 0)
            {
                command[len]    = 0;

                if ((command[0] != 'T') && (command[0] != '\n') && (command[0] != '\r'))
                {
                    //printf("command: %s\n", command);
                }
            }
        }

        printf("ImageProcessingServer receiver exit\n");
    }

    void Run() {
        printf("ImageProcessingServer started\n");

        signal(SIGPIPE, SIG_IGN);

        try {
            if (listen(PortNo)) {
                while (true) {
                    printf("Waiting for processing connection\n");

                    if (accept()) {
                        printf("Connected\n");

                        m_connected = true;

                        //StartThreads();

                        // START POSE CODE
                        
                        // Declare RealSense pipeline, encapsulating the actual device and sensors
                        rs2::pipeline pipe;
                        // Create a configuration for configuring the pipeline with a non default profile
                        rs2::config cfg;
                        // Add pose stream
                        cfg.enable_stream(RS2_STREAM_POSE, RS2_FORMAT_6DOF);
                        // Start pipeline with chosen configuration
                        pipe.start(cfg);

                        char msg[256];

                        // Main loop
                        int frame  = -1;
                        while (true) {
				auto frames = pipe.wait_for_frames();
                        	++frame;
                        	if(frame % 20 != 0) { continue; }
                            // Wait for the next set of frames from the camera
                            //auto frames = pipe.wait_for_frames();
                            // Get a frame from the pose stream
                            auto f = frames.first_or_default(RS2_STREAM_POSE);
                            // Cast the frame to pose_frame and get its data
                            auto pose_data = f.as<rs2::pose_frame>().get_pose_data();

                            //double x = pose_data.translation.x * inches_per_metre;
                            //double y = -pose_data.translation.z * inches_per_metre;
                            double x = pose_data.translation.x * feet_per_metre;
                            double y = -pose_data.translation.z * feet_per_metre;

                            // https://github.com/IntelRealSense/librealsense/issues/3129#issuecomment-475782301
                            // Convert quarternions into yaw
                            double qw = pose_data.rotation.w;
                            double qx = pose_data.rotation.x;
                            double qz = pose_data.rotation.y; // Swapped in weird frame
                            double qy = pose_data.rotation.z;

                            double yaw = atan2(2.0*(qx*qy + qw*qz), qw*qw + qx*qx - qy*qy - qz*qz) * degrees_per_radian;

                            snprintf(msg, sizeof(msg), "P %lf %lf %lf %d %d\n",
                                x, y, yaw, (int)pose_data.tracker_confidence,
                                (int)pose_data.mapper_confidence);
							//printf(msg);
                            if (write((void *) msg, strlen(msg)) == ERROR) {
                                printf("write failed\n");
                                break;
                            }       
                        }


                        m_connected = false;

                        Disconnect();

//                      ::shutdown(m_connectedSocket,SHUT_RDWR);
//                      ::close(m_connectedSocket);
//                      m_connectedSocket = ERROR;

                        printf("Processing Connection Lost\n");
                    }
                }
            }
        } catch (...) {
            printf("Caught exception\n");
        }
    }
} NetworkServer;



int main(int argc, char * argv[]) {
    try {
        NetworkServer.StartServer();
        while(true) { sleepMs(1000); }
    } catch(...) {
    }
    return EXIT_SUCCESS;
}
