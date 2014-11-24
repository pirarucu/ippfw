#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>


int closeTimeout = 10;
pid_t chdTbl[500];
int chdNum = 0;


void ippfw(int nso, struct sockaddr_in *pip)
{
    int i, j, pso, len, max, non, pon, nlen, plen;
    char nbuf[2048], pbuf[2048];
    fd_set rfs, wfs;
    struct timeval tv;
    time_t ct, now;

    if ((pso = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    if (connect(pso, (struct sockaddr *) pip, sizeof(*pip)) < 0)
    {
        perror("connect");
        exit(1);
    }

    max = (pso > nso ? pso : nso) + 1;
    plen = nlen = 0;
    non = pon = 1;
    ct = 0;

    while (1)
    {
        FD_ZERO(&rfs);
        if (non && nlen < sizeof(nbuf)) FD_SET(nso, &rfs);
        if (pon && plen < sizeof(pbuf)) FD_SET(pso, &rfs);
        FD_ZERO(&wfs);

        if (nlen > 0) FD_SET(pso, &wfs);
        if (plen > 0) FD_SET(nso, &wfs);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(max, &rfs, &wfs, NULL, &tv) < 0)
        {
            perror("select");
            exit(1);
        }

        if (FD_ISSET(nso, &rfs) && nlen < sizeof(nbuf))
        {
            if ((len = recv(nso, nbuf + nlen, sizeof(nbuf) - nlen, 0)) < 0)
                exit(0);
            if (len == 0)
                non = 0;
            nlen += len;
        }

        if (FD_ISSET(pso, &rfs) && plen < sizeof(pbuf))
        {
            if ((len = recv(pso, pbuf + plen, sizeof(pbuf) - plen, 0)) < 0)
                exit(0);
            if (len == 0)
                pon = 0;
            plen += len;
        }

        if (FD_ISSET(pso, &wfs) && nlen > 0)
        {
            if ((len = send(pso, nbuf, nlen, 0)) < 0)
                exit(0);
            for (i = len, j = 0; i < nlen; nbuf[j++] = nbuf[i++]);
            nlen -= len;
        }

        if (FD_ISSET(nso, &wfs) && plen > 0)
        {
            if ((len = send(nso, pbuf, plen, 0)) < 0)
                exit(0);
            for (i = len, j = 0; i < plen; pbuf[j++] = pbuf[i++]);
            plen -= len;
        }

        if (!non || !pon)
        {
            if (nlen == 0 && plen == 0)
                exit(0);
            if (ct)
            {
                time(&now);
                if (now - ct >= closeTimeout)
                    exit(0);
            }
            else
                time(&ct);
        }
    }
}

void sigChld(int sig)
{
    int i, j, st;

    for (i = 0; i < chdNum; i++)
    {
        if (waitpid(chdTbl[i], &st, WNOHANG) == 0)
            continue;
        for (j = i, chdNum--; j < chdNum; j++)
            chdTbl[j] = chdTbl[j + 1];
    }
}

int main(int argc, char **argv)
{
    struct sockaddr_in lip, pip, aip;
    unsigned short lport, pport;
    int lso, nso, val, len;
    pid_t pid;

    if (argc != 5)
    {
        printf("Usage: %s {local ip} {local port} {peer ip} {peer port}\n", argv[0]);
        exit(0);
    }
    
    memset(&lip, 0, sizeof(lip));
    lip.sin_family = AF_INET;
    lip.sin_addr.s_addr = inet_addr(argv[1]);
    lip.sin_port = htons(atoi(argv[2]));

    memset(&pip, 0, sizeof(pip));
    pip.sin_family = AF_INET;
    pip.sin_addr.s_addr = inet_addr(argv[3]);
    pip.sin_port = htons(atoi(argv[4]));

    if ((lso = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("open socket");
        exit(1);
    }

    val = 1;
    setsockopt(lso, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (bind(lso, (struct sockaddr *) &lip, sizeof(lip)) < 0)
    {
        perror("bind");
        close(lso);
        exit(1);
    }

    if (listen(lso, 5) < 0)
    {
        perror("listen");
        close(lso);
        exit(1);
    }

    setpgrp();

    if ((pid = fork()) < 0)
    {
        perror("fork");
        close(lso);
        exit(1);
    }

    if (pid > 0)
    {
        printf("Startup successfully\n");
        exit(0);
    }

    signal(SIGCHLD, sigChld);
    signal(SIGUSR1, sigChld);

    while (1)
    {
        len = sizeof(aip);
        if ((nso = accept(lso, (struct sockaddr *) &aip, &len)) < 0)
        {
            perror("accept");
            continue;
        }

        if (chdNum >= sizeof(chdTbl))
        {
            printf("Too many childs\n");
            close(nso);
            continue;
        }

        if ((pid = fork()) < 0)
        {
            perror("fork");
            continue;
        }

        if (pid == 0)
        {
            close(lso);
            setpgrp();
            ippfw(nso, &pip);
            exit(0);
        }
        else
        {
            close(nso);
            chdTbl[chdNum++] = pid;
        }
    }
}
