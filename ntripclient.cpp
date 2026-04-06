#include "ntripclient.h"

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

// ================= SERIAL INIT =================
static int init_serial(const char* dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("Serial open");
        return -1;
    }

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
    tcflush(fd, TCIOFLUSH);

    return fd;
}

// ================= CHECKSUM =================
static int verifyChecksum(const char *sentence)
{
    if (!sentence || sentence[0] != '$')
        return 0;

    uint8_t calc = 0;
    const char *ptr = sentence + 1;

    while (*ptr && *ptr != '*')
        calc ^= *ptr++;

    if (*ptr != '*')
        return 0;

    ptr++;

    if (strlen(ptr) < 2)
        return 0;

    char cs[3] = {ptr[0], ptr[1], '\0'};
    uint8_t given = (uint8_t)strtol(cs, NULL, 16);

    return (calc == given);
}

// ================= LAT/LON FILTER =================
static double lat_filt(float def)
{
    float k = def * 0.01;
    int deg = (int)k;

    float sec = (def - deg * 100) / 60.0;
    return deg + sec;
}

static double lng_filt(float def)
{
    float k = def * 0.01;
    int deg = (int)k;

    float sec = (def - deg * 100) / 60.0;
    return deg + sec;
}

// ================= GGA PARSER =================
static void gga_nmeaparser(char *ga,
                           int *timg, double *latg, char *l_dirg,
                           double *lngvg, char *ln_dirg,
                           int *qa, int *nsat,
                           float *hdp, float *alt,
                           int *difa, int *difs)
{
    if (strstr(ga, "$GNGGA") || strstr(ga, "$GPGGA"))
    {
        char *tok = strtok(ga, ",");
        int z = 0;

        while (tok != NULL)
        {
            if (z == 1) *timg = atoi(tok);
            if (z == 2) *latg = lat_filt(atof(tok));
            if (z == 3) *l_dirg = tok[0];
            if (z == 4) *lngvg = lng_filt(atof(tok));
            if (z == 5) *ln_dirg = tok[0];
            if (z == 6) *qa = atoi(tok);
            if (z == 7) *nsat = atoi(tok);
            if (z == 8) *hdp = atof(tok);
            if (z == 9) *alt = atof(tok);
            if (z == 13) *difa = atoi(tok);
            if (z == 14) *difs = atoi(tok);

            tok = strtok(NULL, ",");
            z++;
        }
    }
}

// ================= RMC PARSER =================
static void rmc_nmeaparser(char *bu,
                           int *timk, char *va,
                           double *latv, char *l_dir,
                           double *lngv, char *ln_dir,
                           double *sp, double *hea,
                           int *da, char *fa)
{
    if (strstr(bu, "$GNRMC") || strstr(bu, "$GPRMC"))
    {
        char *tok = strtok(bu, ",");
        int z = 0;

        while (tok != NULL)
        {
            if (z == 1) *timk = atoi(tok);
            if (z == 2) *va = tok[0];
            if (z == 3) *latv = lat_filt(atof(tok));
            if (z == 4) *l_dir = tok[0];
            if (z == 5) *lngv = lng_filt(atof(tok));
            if (z == 6) *ln_dir = tok[0];
            if (z == 7) *sp = atof(tok);
            if (z == 8) *hea = atof(tok);
            if (z == 9) *da = atoi(tok);
            if (z == 10) *fa = tok[0];

            tok = strtok(NULL, ",");
            z++;
        }
    }
}

// ================= SOCKET =================
static int connect_socket(QString host, int port, QString request)
{
    struct hostent *server;
    struct sockaddr_in serv_addr;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    server = gethostbyname(host.toStdString().c_str());
    if (server == NULL) return -1;

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);

    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        return -1;

    write(sockfd, request.toStdString().c_str(), request.length());

    return sockfd;
}

// ================= CONSTRUCTOR =================
NtripClient::NtripClient(QObject *parent)
    : QObject(parent)
{
    tdata.running = false;
}

// ================= FETCH =================
void NtripClient::fetchMountPoints(QString host, int port)
{
    std::thread([=]() {

        QStringList list;
        QString req = "GET / HTTP/1.0\r\nUser-Agent: NTRIPClient\r\n\r\n";

        int sock = connect_socket(host, port, req);

        if (sock < 0) {
            emit mountPointsReceived(QStringList() << "Connection Failed");
            return;
        }

        char buffer[4096];

        while (1) {
            int n = read(sock, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;

            buffer[n] = '\0';

            QString data(buffer);
            QStringList lines = data.split("\n");

            for (QString line : lines) {
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
    if (tdata.running) return;

    emit connectionStatus("Connecting...");

    int serial_fd = init_serial("/dev/ttyUSB0");
    if (serial_fd < 0) {
        emit connectionStatus("Serial Failed");
        return;
    }

    tdata.serial_fd = serial_fd;
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

    pthread_join(ntrip_tid, NULL);
    pthread_join(serial_tid, NULL);

    if (tdata.serial_fd > 0)
        close(tdata.serial_fd);

    emit connectionStatus("Disconnected");
}

// ================= NTRIP THREAD =================
void* NtripClient::ntrip_thread(void *arg)
{
    ThreadData *data = (ThreadData*)arg;

    while (data->running)
    {
        QString request =
            "GET /" + data->mountpoint + " HTTP/1.1\r\n"
            "Host: " + data->host + "\r\n"
            "Ntrip-Version: Ntrip/2.0\r\n"
            "User-Agent: NTRIPClient\r\n"
            "Authorization: " + data->auth + "\r\n"
            "Connection: keep-alive\r\n\r\n";

        int sock = connect_socket(data->host, data->port, request);

        if (sock < 0) {
            sleep(2);
            continue;
        }

        char buffer[1024];

        while (data->running)
        {
            int n = read(sock, buffer, sizeof(buffer));

            if (n > 0)
                write(data->serial_fd, buffer, n);
            else
                break;
        }

        close(sock);
        sleep(2);
    }

    return NULL;
}

// ================= SERIAL THREAD =================
void* NtripClient::serial_thread(void *arg)
{
    ThreadData *data = (ThreadData*)arg;

    char line[256];
    int idx = 0;
    char c;

    int tim=0, dat=0, qua=0, nsati=0;
    double lat=0, lng=0, spd=0, hd=0;
    char valid=0, ltdir=0, lngdir=0, fixt=0;

    while (data->running)
    {
        if (read(data->serial_fd, &c, 1) <= 0)
            continue;

        if (c == '\n')
        {
            line[idx] = '\0';
            idx = 0;

            if (line[0] != '$' || !verifyChecksum(line))
                continue;

            char temp[256];
            strcpy(temp, line);

            if (strstr(line, "GGA"))
            {
                int diff_age=0, diff_sat=0;
                float hdop=0, alti=0;

                gga_nmeaparser(temp, &tim, &lat, &ltdir, &lng, &lngdir,
                               &qua, &nsati, &hdop, &alti,
                               &diff_age, &diff_sat);
            }
            else if (strstr(line, "RMC"))
            {
                rmc_nmeaparser(temp, &tim, &valid, &lat, &ltdir,
                               &lng, &lngdir, &spd, &hd,
                               &dat, &fixt);
            }

            QString out = QString("Lat:%1 Lon:%2 Fix:%3 Sats:%4 Spd:%5 Head:%6")
                    .arg(lat, 0, 'f', 6)
                    .arg(lng, 0, 'f', 6)
                    .arg(qua)
                    .arg(nsati)
                    .arg(spd, 0, 'f', 2)
                    .arg(hd, 0, 'f', 1);

            QMetaObject::invokeMethod(data->self,
                                     "dataUpdated",
                                     Qt::QueuedConnection,
                                     Q_ARG(QString, out));
        }
        else if (c != '\r' && idx < sizeof(line) - 1)
        {
            line[idx++] = c;
        }
    }

    return NULL;
}
