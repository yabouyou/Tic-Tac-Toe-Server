#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct item {
    int clientfd;
    struct in_addr ip;
    int player;
    char *buf;
    int bytes_in_buf;
    struct item *next;
};

struct item *head = NULL;

void insert(int clientfd, struct in_addr ip, int player)
{
    struct item *new, **pp;

    /* create the new item */
    if ((new = malloc(sizeof(struct item))) == NULL) {
        fprintf(stderr, "out of memory!\n");  /* unlikely */
        exit(1);
    }
    new->clientfd = clientfd;
    new->ip = ip;
    new->player = player;
    new->bytes_in_buf = 0;
    new->buf = malloc(sizeof(char) * 1000);

    /* find the (struct item *) to place it at */
    for (pp = &head; *pp && (*pp)->clientfd < clientfd; pp = &(*pp)->next);

    /* link it in */
    new->next = *pp;
    *pp = new;

}

void delete(int clientfd)
{
    struct item **pp;

    for (pp = &head; *pp && (*pp)->clientfd < clientfd; pp = &(*pp)->next);

    if (*pp && (*pp)->clientfd == clientfd) {
        struct item *next = (*pp)->next;
        free(*pp);
        *pp = next;
    }
}

char board[9] = {'1', '2', '3', 
                 '4', '5', '6',
                 '7', '8', '9'};

char* getipstring(struct in_addr q){
    char* ip = malloc(INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(q), ip, INET_ADDRSTRLEN);
    return ip;
}

void showboard(int fd)
{
    char buf[100], *bufp, *boardp;
    int col, row;

    for (bufp = buf, col = 0, boardp = board; col < 3; col++) {
        for (row = 0; row < 3; row++, bufp += 4)
            sprintf(bufp, " %c |", *boardp++);
        bufp -= 2;  // kill last " |"
        strcpy(bufp, "\r\n---+---+---\r\n");
        bufp = strchr(bufp, '\0');
    }
    if (write(fd, buf, bufp - buf) != bufp-buf)
	    perror("write");
}


int game_is_over()  /* returns winner, or ' ' for draw, or 0 for not over */
{
    int i, c;
    extern int allthree(int start, int offset);
    extern int isfull();

    for (i = 0; i < 3; i++)
        if ((c = allthree(i, 3)) || (c = allthree(i * 3, 1)))
            return(c);
    if ((c = allthree(0, 4)) || (c = allthree(2, 2)))
        return(c);
    if (isfull())
        return(' ');
    return(0);
}

int allthree(int start, int offset)
{
    if (board[start] > '9' && board[start] == board[start + offset]
            && board[start] == board[start + offset * 2])
        return(board[start]);
    return(0);
}

int isfull()
{
    int i;
    for (i = 0; i < 9; i++)
        if (board[i] < 'a')
            return(0);
    return(1);
}

char *extractline(char *p, int size)
	/* returns pointer to string after, or NULL if there isn't an entire
	 * line here.  If non-NULL, original p is now a valid string. */
{
    int nl;
    for (nl = 0; nl < size && p[nl] != '\r' && p[nl] != '\n'; nl++)
	;
    if (nl == size)
	return(NULL);

    /*
     * There are three cases: either this is a lone \r, a lone \n, or a CRLF.
     */
    if (p[nl] == '\r' && nl + 1 < size && p[nl+1] == '\n') {
	/* CRLF */
	p[nl] = '\0';
	return(p + nl + 2);
    } else {
	/* lone \n or \r */
	p[nl] = '\0';
	return(p + nl + 1);
    }
}

void movemsg(int clientfd, int turn){
    char xTurnMsg[] = "It is x's turn.\r\n";
    char oTurnMsg[] = "It is o's turn.\r\n";

    if (turn == 0){
        if (write(clientfd, xTurnMsg, sizeof xTurnMsg - 1) != sizeof xTurnMsg - 1){
            perror("write");
            exit(1);
        }
    }
    else {
        if (write(clientfd, oTurnMsg, sizeof oTurnMsg - 1) != sizeof oTurnMsg - 1){
            perror("write");
            exit(1);
        }
    }
}

void attemptMove(struct item client, int square, int *turn){

    if (client.player == 0 || client.player == 1){
        int currTurn = *turn;
        if (client.player != currTurn){
            if (write(client.clientfd, "It is not your turn\r\n", 21) != 21){
                perror("write");
                exit(1);
            }
            printf("%s tries to make move %d, but it's not their turn\n", getipstring(client.ip), square);
            return;
        }
        if (isdigit(board[square - 1])){
        
            board[square - 1] = "xo"[client.player];
            
            printf("%s (%c) makes move %d \n", getipstring(client.ip), "xo"[currTurn], square);

            if (currTurn == 1)
                *turn = 0;
            else
                *turn = 1;

            struct item *p;

            for (p = head; p; p = p->next) {
                showboard(p->clientfd);

                char turnMsg[] = "It is your turn.\r\n";
                
                if (*turn == p->player){
                    if (write(p->clientfd, turnMsg, sizeof turnMsg - 1) != sizeof turnMsg - 1){
                        perror("write");
                        exit(1);
                    }
                }
                else{
                    movemsg(p->clientfd, *turn);
                }
            } 
        }
        else{
            char msg[] =  "That space is taken\r\n";
            if (write(client.clientfd, msg, sizeof msg - 1) != sizeof msg - 1){
                perror("write");
                exit(1);
            }
        }
    }
    else if (client.player == 2){
        char msg[] = "You can't make moves; you can only watch the game\r\n";
        if (write(client.clientfd, msg, sizeof msg - 1) != sizeof msg - 1){
            perror("write");
            exit(1);
        }
    }
    else {
        fprintf(stderr, "AttemptMove: unexpected value %d", client.player);
        exit(1);
    }
}

void setNewPlayer(int player, int *spots){
    struct item *p;
    for (p = head; p; p = p->next) {
        if (p->player == 2){
            p->player = player;
            spots[player] = 1;
            char playmsg[] = "You now get to play!  You are now _.\r\n";
            playmsg[strlen(playmsg) - 4] = "xo"[player];

            if (write(p->clientfd, playmsg, sizeof playmsg - 1) != sizeof playmsg -1){
                perror("write");
                exit(1);
            }
            printf("client from %s is now %c\r\n", getipstring(p->ip), "xo"[player]);
            break;

        }
    }
}

void writeToAll(char* msg, int bytes){
    struct item *p;
    for (p = head; p; p = p->next) {
        if (write(p->clientfd, msg, bytes) != bytes){
            perror("write");
            exit(1);
        }
    }
}

void writeToAllOthers(char* msg, int bytes, int fd){
    struct item *p;
    for (p = head; p; p = p->next) {
        if (p->clientfd != fd){
            if (write(p->clientfd, msg, bytes) != bytes){
                perror("write");
                exit(1);
            }
        }
    }
}

void switchPlayerSymbol(){
    struct item *p;
    for (p = head; p; p = p->next) {
        if (p->player == 0 || p->player == 1) {
            // switch the bit using xor
            p->player = p->player^1;
            char msg[] = "You are _.\r\n";
            msg[strlen(msg) - 4] = "xo"[p->player];
            if (write(p->clientfd, msg, strlen(msg) ) != strlen(msg)){
                perror("write");
                exit(1);
            }
        }
        showboard(p->clientfd);
    }
}


int main(int argc, char **argv)
{
    extern void insert(int clientfd, struct in_addr ip, int player), delete(int clientfd), printall();
    extern int search(int clientfd);
    int clientfd, c, port = 3000, status = 0;
    int listen_soc;
    socklen_t size;
    struct sockaddr_in r, q;
    fd_set fds, readyfds;

    while ((c = getopt(argc, argv, "p:")) != EOF) {
	switch (c) {
	case 'p':
	    port = atoi(optarg);
	    break;
	default:
	    status = 1;
	}
    }
    if (status || optind < argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        return(1);
    }

    if ((listen_soc = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);

    if (bind(listen_soc, (struct sockaddr *)&r, sizeof r) < 0) {
        perror("bind");
        return(1);
    }
    if (listen(listen_soc, 5)) {
	perror("listen");
	return(1);
    }

    // put socket fd into set
    FD_ZERO(&fds);
    FD_SET(listen_soc, &fds);

    int turn = 0;
    int clientcount = 0;
    int maxfd = listen_soc;
    int playerSpotTaken[2] = {0, 0};

    while (1) {
        int res = game_is_over();
        if (res) {
            printf("Game Over!\n");
            // notify clients of result
            char winMsg[] = "_ wins.\r\n";
            char drawMsg[] = "It is a draw.\r\n";
            char playAgainMsg[] = "Let's play again!\r\n";
            if (res == ' '){ 
                writeToAll(drawMsg, sizeof drawMsg - 1);
            }
            else{
                winMsg[0] = res;
                writeToAll(winMsg, sizeof winMsg - 1);
                printf("%s", winMsg);
            }
            
            writeToAll(playAgainMsg, sizeof playAgainMsg - 1);

            // reset board
            turn = 0;
            for (int i = 0; i < 9; i++){
                board[i] = '1' + i;
            }

            switchPlayerSymbol();
        }

        readyfds = fds;

        // TODO: case if select is 0
        if (select(maxfd + 1, &readyfds, NULL, NULL, NULL) <= 0) {
	        perror("select");
	        return(1);
        }

        // new client connected
        if (FD_ISSET(listen_soc, &readyfds)){
            size = sizeof q;

            if ((clientfd = accept(listen_soc, (struct sockaddr *)&q, &size)) < 0) {
                perror("accept");
                return(1);
            }

            if (clientfd > maxfd){
                maxfd = clientfd;
            }
            // display board and current turn to client
            showboard(clientfd);
            movemsg(clientfd, turn);

            // insert new node in linked list for client
            insert(clientfd, q.sin_addr, 2);

            for (int i = 0; i < 2; i++){
                if (!playerSpotTaken[i]){
                    setNewPlayer(i, playerSpotTaken);
                    break;
                }
            }

            
            // insert new fd into fd set
            FD_SET(clientfd, &fds);

            // update # of clients
            clientcount++;


            printf("new connection from %s\n", getipstring(q.sin_addr));
        }
        
        // iterate through each client
        // check for message/move
        struct item *p;
        for (p = head; p; p = p->next) {
            int len;
            int currfd = p->clientfd;
            char *buf = p->buf;
            int bytes = p->bytes_in_buf;
            if (FD_ISSET(currfd, &readyfds)){
                if ((len = read(currfd, buf + bytes, 1000 - bytes - 1)) < 0) {
                    perror("read");
                    return(1);
                }
                if (len == 0){
                    printf("disconnecting client %s\n", getipstring(p->ip));
                    delete(currfd);
                    FD_CLR(currfd, &fds);
                    playerSpotTaken[p->player] = 0;

                    if (p->player == 0 || p->player == 1){
                        setNewPlayer(p->player, playerSpotTaken);
                    }
                    clientcount--;
                }
                else{
                    p->bytes_in_buf += len;
                    int size = p->bytes_in_buf;
                    char *nextpos;

                    while ((nextpos = extractline(buf, size))){
                        int bufSize = strlen(buf);
                        char c = buf[0];
                        if (bufSize == 1 && isdigit(c) && ((c - '0') != 0)){
                            int digit = c - '0';
                            attemptMove(*p, digit, &turn);
                        }
                        else {
                            printf("chat message: %s\n", buf);
                            strncat(buf, "\r\n", 2);
                            writeToAllOthers(buf, strlen(buf), p->clientfd);
                        }
                        size -= nextpos - buf;
                        memmove(buf, nextpos, size);
                    }

                    if (!size){
                        free(p->buf);
                        char* newbuf = malloc(1000);
                        p->buf = newbuf;
                        p->bytes_in_buf = 0;
                    }

                }
            }
        }
    }
    return(status);
}


