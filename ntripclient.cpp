#include "ntripclient.h"

#include <QByteArray>
#include <QMetaObject>

#include <stdio.h>
#include <thread>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// ================= SERIAL =================
static int init_serial(const char* dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;

    struct termios tio;
    tcgetattr(fd, &tio);

    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;

    tio.c_lflag = 0;
    tio.c_oflag = 0;
    tio.c_iflag = 0;

    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tio);
    return fd;
}

// ================= CHECKSUM =================
static int verifyChecksum(const char *s)
{
    if (!s || s[0] != '$') return 0;

    uint8_t calc = 0;
    const char *p = s + 1;

    while (*p && *p != '*')
        calc ^= *p++;

    if (*p != '*') return 0;
    p++;

    char cs[3] = {p[0], p[1], '\0'};
    uint8_t given = strtol(cs, NULL, 16);

    return calc == given;
}

// ================= LAT/LON =================
static double lat_filt(float d)
{
    int deg = (int)(d * 0.01);
    return deg + (d - deg * 100) / 60.0;
}

static double lng_filt(float d)
{
    int deg = (int)(d * 0.01);
    return deg + (d - deg * 100) / 60.0;
}

// ================= GGA =================
static void parseGGA(char *buf, double *lat, double *lon, int *fix, int *sat)
{
    char *tok = strtok(buf, ",");
    int i = 0;

    while (tok)
    {
        if (i == 2) *lat = lat_filt(atof(tok));
        if (i == 4) *lon = lng_filt(atof(tok));
        if (i == 6) *fix = atoi(tok);
        if (i == 7) *sat = atoi(tok);

        tok = strtok(NULL, ",");
        i++;
    }
}

// ================= RMC =================
static void parseRMC(char *buf, double *spd, double *hd)
{
    char *tok = strtok(buf, ",");
    int i = 0;

    while (tok)
    {
        if (i == 7) *spd = atof(tok);
        if (i == 8) *hd  = atof(tok);

        tok = strtok(NULL, ",");
        i++;
    }
}

// ================= SOCKET =================
static int connect_socket(QString host, int port, QString request)
{
    struct hostent *server;
    struct sockaddr_in serv_addr;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    server = gethostbyname(host.toStdString().c_str());
    if (!server) return -1;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        return -1;

    write(sock, request.toStdString().c_str(), request.length());

    return sock;
}

// ================= CONSTRUCTOR =================
NtripClient::NtripClient(QObject *parent) : QObject(parent)
{
    tdata.running = false;
    tdata.serial_fd = -1;
    ntrip_tid = 0;
    serial_tid = 0;
}

// ================= FETCH =================
void NtripClient::fetchMountPoints(QString host, int port)
{
    std::thread([=]() {

        QStringList list;
        QString req = "GET / HTTP/1.0\r\n\r\n";

        int sock = connect_socket(host, port, req);
        if (sock < 0) {
            emit mountPointsReceived(QStringList());
            return;
        }

        char buffer[4096];

        while (1) {
            int n = read(sock, buffer, sizeof(buffer)-1);
            if (n <= 0) break;

            buffer[n] = '\0';
            QString data(buffer);

            for (QString line : data.split("\n")) {
                if (line.startsWith("STR;")) {
                    QStringList parts = line.split(";");
                    if (parts.size() > 1)
                        list.append(parts[1].trimmed());
                }
            }
        }

        close(sock);
        emit mountPointsReceived(list);

    }).detach();
}

// ================= CONNECT =================
void NtripClient::connectToMountPoint(QString host, int port,
                                      QString mountpoint,
                                      QString auth)
{
    if (tdata.running) {
        emit connectionStatus("Already Connected");
        return;
    }

    int fd = init_serial("/dev/ttyUSB0");
    if (fd < 0) {
        emit connectionStatus("Serial Error");
        return;
    }

    tdata.serial_fd = fd;
    tdata.host = host;
    tdata.port = port;
    tdata.mountpoint = mountpoint;
    tdata.auth = auth;
    tdata.running = true;
    tdata.self = this;

    pthread_create(&ntrip_tid, NULL, ntrip_thread, &tdata);
    pthread_create(&serial_tid, NULL, serial_thread, &tdata);

    emit connectionStatus("Connected");
}

// ================= DISCONNECT =================
void NtripClient::disconnectClient()
{
    if (!tdata.running) return;

    emit connectionStatus("Disconnecting...");
    tdata.running = false;

    if (ntrip_tid) pthread_join(ntrip_tid, NULL);
    if (serial_tid) pthread_join(serial_tid, NULL);

    if (tdata.serial_fd > 0) {
        close(tdata.serial_fd);
        tdata.serial_fd = -1;
    }

    emit connectionStatus("Disconnected");
}

// ================= NTRIP THREAD =================
void* NtripClient::ntrip_thread(void *arg)
{
    ThreadData *d = (ThreadData*)arg;

    while (d->running)
    {
        QString auth = "Basic " + d->auth.toUtf8().toBase64();

        QString req =
            "GET /" + d->mountpoint + " HTTP/1.1\r\n"
            "Host: " + d->host + "\r\n"
            "Authorization: " + auth + "\r\n\r\n";

        int sock = connect_socket(d->host, d->port, req);

        if (sock < 0) {
            sleep(2);
            continue;
        }

        char buf[1024];

        while (d->running) {
            int n = read(sock, buf, sizeof(buf));
            if (n > 0)
                write(d->serial_fd, buf, n);
            else break;
        }

        close(sock);
        sleep(2);
    }

    return NULL;
}

// ================= SERIAL THREAD =================
void* NtripClient::serial_thread(void *arg)
{
    ThreadData *d = (ThreadData*)arg;

    char line[256];
    int idx = 0, c;

    double lat=0, lon=0, spd=0, hd=0;
    int fix=0, sat=0;

    while (d->running)
    {
        if (read(d->serial_fd, &c, 1) <= 0)
            continue;

        if (c == '\n')
        {
            line[idx] = '\0';
            idx = 0;

            if (!verifyChecksum(line))
                continue;

            char temp[256];
            strcpy(temp, line);

            if (strstr(line, "GGA"))
                parseGGA(temp, &lat, &lon, &fix, &sat);

            else if (strstr(line, "RMC"))
                parseRMC(temp, &spd, &hd);

            QString out = QString("Lat:%1 Lon:%2 Fix:%3 Sat:%4 Spd:%5 Head:%6")
                    .arg(lat,0,'f',6).arg(lon,0,'f',6)
                    .arg(fix).arg(sat)
                    .arg(spd,0,'f',2).arg(hd,0,'f',1);

            QMetaObject::invokeMethod(d->self, "dataUpdated",
                Qt::QueuedConnection, Q_ARG(QString, out));
        }
        else if (c != '\r' && idx < 255)
            line[idx++] = c;
    }

    return NULL;
}
