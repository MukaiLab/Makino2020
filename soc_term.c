/*9
 * Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

static const char prog_name[] = "soc_term";

/* termios構造体
/*struct termios {
/*    tcflag_t c_iflag;       /* input mode flags */
/*    tcflag_t c_oflag;       /* output mode flags */
/*    tcflag_t c_cflag;       /* control mode flags */
/*    tcflag_t c_lflag;       /* local mode flags */
/*    cc_t c_line;            /* line discipline */
/*    cc_t c_cc[NCCS];        /* control characters */
/*};
*/
static struct termios old_term;

static bool handle_telnet;

static void usage(void)
{
	fprintf(stderr, "Usage: %s [-t] <port>\n", prog_name);
	fprintf(stderr, "\t-t: handle telnet commands\n");
	exit(1);
}

static int get_port(const char *str)
{
	long port;
	char *eptr;

	if (*str == '\0')
		usage();

	port = strtol(str, &eptr, 10);// 文字列をlong値に変換
	printf("port= %ld\n", port);
	if (port < 1 || *eptr != '\0')
		usage();
	return (int)port;
}

static int get_listen_fd(const char *port_str)
{
	struct sockaddr_in sain; //最初に構造体を宣言
	int fd;
	int on;
	int port = get_port(port_str);

	memset(&sain, 0, sizeof(sain));	// memset()で構造体を0で埋める
	sain.sin_family = AF_INET;	// IPv4
	sain.sin_port = htons(port);	// sin.sin_portを使用するポート番号で初期化
	sain.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY はそのマシンのどのネットワーク装置からの アクセスも受け付ける.どのアドレスに来た要求も受け付ける

	fd = socket(sain.sin_family, SOCK_STREAM, 0); //通信のための端点 (endpoint) を作成
	if (fd == -1)
		err(1, "socket");

	on = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
		err(1, "setsockopt");

	if (bind(fd, (struct sockaddr *)&sain, sizeof(sain))) // サーバ側で利用するアドレス(IPアドレスとポート番号)を設定
		err(1, "bind");

	if (listen(fd, 5)) // 要求受付を開始, 第二引数:最大何個のクライアントを接続要求で待たせることができるか（待ち行列の長さ）5が一般的
		err(1, "listen");

	return fd;

}

static int accept_fd(int listen_fd) // 接続を受ける
{
	struct sockaddr_storage sastor;
	socklen_t slen = sizeof(sastor);
	int fd = accept(listen_fd, (struct sockaddr *)&sastor, &slen); // ソケットの接続待機

	if (fd == -1)
		err(1, "accept");
	return fd;
}

static void save_current_termios(void) // 現在のターミナル情報を取得（標準入力および出力のターミナル構成）
{
	/* STDIN_FILENO(標準入力)構造体の現在の値を&fで示す構造体に書き込む */
	if (tcgetattr(STDIN_FILENO, &old_term) == -1) 
		err(1, "save_current_termios: tcgetattr");
}

static void restore_termios(void) // 現在のtermiosの設定を保存(もとに戻す？)
{
	// oldtermの情報により、ターミナルににパラメータを設定
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term) == -1) //TCSAFLUSHすぐに変更を適用
		err(1, "restore_termios: tcsetattr");
}

static void set_tty_noncanonical(void) //現在の端末の情報をコピーして他のパラメーターを構成し、tcsetattr関数を使用して現在起動している端末の情報を設定
{
	int fd = STDIN_FILENO; // 0
	struct termios t;

	t = old_term; // old_termという名前のtermios構造体

	/* 
	   old_termのc_lflagsメンバはローカルモードについてのメンバ
	   ICANON：カノニカルモードを有効にする(入力は行単位で行われるモード)
	　 ECHO	 ：入力された文字をエコー
	   ISIG  ：INTR, QUIT, SUSP, DSUSP の文字を受信した時、対応するシグナルを 発生させる
	*/

	t.c_lflag &= ~(ICANON | ECHO | ISIG);

	t.c_iflag &= ~ICRNL; //  入力のCRをNL(LF) に置き換える(両方行末記号を表す.これを行わないと，他のコンピュータでCR を入力しても，入力が終りにならない)

	t.c_cc[VMIN] = 1;                   /* Character-at-a-time input(1度に1文字ずつ入力) 非カノニカル読み込み時の最小文字数*/
	t.c_cc[VTIME] = 0;                  /* with blocking 非カノニカル読み込み時のタイムアウト時間 (1/10 秒単位) */

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == -1) 
		err(1, "set_tty_noncanonical: tcsetattr");
}

static bool write_buf(int fd, const void *buf, size_t count)
{
	const uint8_t *b = buf;
	size_t num_written = 0;

	while (num_written < count) {
		ssize_t res = write(fd, b + num_written, count - num_written);

		if (res == -1)
			return false;

		num_written += res;
	}
	return true;
}

static bool write_file(int fd, int o_fd, const void *buf, size_t count)
{
	const uint8_t *b = buf;
	size_t num_written = 0;

	while (num_written < count) {
		ssize_t res = write(fd, b + num_written, count - num_written);
		ssize_t res1 = write(o_fd, b + num_written, count - num_written);
		if (res == -1)
			return false;
		if (res1 == -1)
			return false;	

		num_written += res;

	}
	
	return true;
}
#define TELNET_IAC		0xff
#define TELNET_DO		0xfd
#define TELNET_WILL		0xfb
#define TELNET_SUPRESS_GO_AHEAD	0x1

static void handle_telnet_codes(int fd, char *buf, size_t *blen)
{
	uint8_t *b = (uint8_t *)buf;
	size_t n = 0;
	static uint8_t cmd_bytes[3];
	static size_t num_cmd_bytes = 0;

	if (!handle_telnet)
		return;

	if (buf == NULL) {
		/* Reset state */
		num_cmd_bytes = 0;
		return;
	}

	for (n = 0;n < *blen; n++) {
		if (num_cmd_bytes || b[n] == TELNET_IAC) {
			cmd_bytes[num_cmd_bytes] = b[n];
			num_cmd_bytes++;
			memmove(b + n, b + n + 1, *blen - n - 1);
			(*blen)--;
			n--;
		}
		if (num_cmd_bytes == 3) {
			switch (cmd_bytes[1]) {
			case TELNET_DO:
				cmd_bytes[1] = TELNET_WILL;
				break;
			case TELNET_WILL:
				if (cmd_bytes[2] == TELNET_SUPRESS_GO_AHEAD) {
					/*
					 * We're done after responding to
					 * this
					 */
					handle_telnet = false;
				}

				cmd_bytes[1] = TELNET_DO;
				break;
			default:
				/* Unknown command, ignore it */
				num_cmd_bytes = 0;
			}
			if (num_cmd_bytes) {
				write_buf(fd, cmd_bytes, num_cmd_bytes);
				num_cmd_bytes = 0;
			}
		}
	}
}

static void serve_fd(int fd, int o_fd)
{
	uint8_t buf[512];
	struct pollfd pfds[2]; // pfds[0]は標準入力, pfds[1]はfd=4

	memset(pfds, 0, sizeof(pfds));
	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;//読み出し可能なデータがある
	pfds[1].fd = fd;
	pfds[1].events = POLLIN;//読み出し可能なデータがある
/*
/*struct pollfd {
/*        int   fd;         /* file descriptor */
/*        short events;     /* requested events */
/*        short revents;    /* returned events */
/*    };
*/

	while (true) {
		size_t n;

		if (poll(pfds, 2, -1) == -1)
			err(1, "poll");
//pfds [0]のPOLLIN時間がトリガーされた場合（端末の標準入力に入力操作があった場合）、読み取り操作
		if (pfds[0].revents & POLLIN) {
			n = read(STDIN_FILENO, buf, sizeof(buf));//端末の標準入力ポートから入力データを読み取る
			if (n == -1)
				err(1, "read stdin");
			if (n == 0)
				errx(1, "read stdin EOF");

			/* TODO handle case when this write blocks */
			//リダイレクトされたポートにバインドされたソケットから読み取ったデータを書き込む
			if (!write_buf(fd, buf, n)) {
				warn("write_buf fd");
				break;
			}
		}

		if (pfds[1].revents & POLLIN) {
			n = read(fd, buf, sizeof(buf));
			if (n == -1) {
				warn("read fd"); // err.h
				break;
			}
			if (n == 0) {
				warnx("read fd EOF"); // err.h
				break;
			}
			handle_telnet_codes(fd, buf, &n);
			//if (!write_buf(STDOUT_FILENO, buf, n))
			if (!write_file(STDOUT_FILENO, o_fd,buf, n))// 読み取ったデータを端末の標準出力に書き込みます
				err(1, "write_file stdout");
		}
	}
}

int main(int argc, char *argv[]) // argc: コマンドライン引数の数
{

	int listen_fd;
	int o_file_fd;
	int fl;
	char *port; // ポート番号. soc_term
	bool have_handle_telnet_option = false;
	char outputfile1[] = "/home/my2020/optee-qemu/tmp/a.txt";
	char outputfile2[] = "/home/my2020/optee-qemu/tmp/b.txt";
	time_t t;
	char fname1[64];
	char fname2[64];

	t = time(NULL);
	strftime(fname1, sizeof(fname1), "/home/my2020/optee-qemu/tmp/TAlog_%Y%m%d%H.txt", localtime(&t));
	strftime(fname2, sizeof(fname2), "/home/my2020/optee-qemu/tmp/CAlog_%Y%m%d%H.txt", localtime(&t));
	printf("%s\n",fname1);
	//FILE *outputfile
	//FILE *outputfile2;
	//outputfile = fopen("/home/my2020/optee-qemu/tmp/soc_log.txt", "w");
	//outputfile2 = fopen("/home/my2020/optee-qemu/tmp/soc_log2.txt", "w");

/*　1. コマンドライン引数のチェック　*/
	
	switch (argc) { // コマンドライン引数２個の場合が通常, 3だとtelnetを使う？(デフォルトはfalse)
	case 2:
		port = argv[1];
		break;
	case 3: // ./soc_term -t <ポート番号>の形だった場合 
		if (strcmp(argv[1], "-t") != 0) // strcmpは第一引数=第二引数のとき0を返す
			usage();
		have_handle_telnet_option = true;
		port = argv[2];
		break;
	default:
		usage();
	}

	

	/*if( outputfile == NULL ) {
		perror("ファイルの読み込みに失敗！\n");
		return 1;
  	}
	if( outputfile2 == NULL ) {
		perror("ファイルの読み込みに失敗！\n");
		return 1;
  	}*/


/*　2. ターミナル情報の保存*/
	save_current_termios();// 現在のターミナル情報を取得（標準入力および出力のターミナル構成）
/*  3. ソケットの設定やリッスン*/
	listen_fd = get_listen_fd(port);
	printf("listening on port %s\n", port);
	printf("listening on port %p\n", port);
	if (have_handle_telnet_option)
		printf("Handling telnet commands\n");
	if(strcmp(port, "54321") == 0){ 
		/*あとでエラー処理を加える*/
		//o_file_fd = open(outputfile1, O_WRONLY | O_APPEND);
		o_file_fd = open(fname1, O_CREAT | O_WRONLY | O_APPEND, S_IRWXU | S_IRWXO);
		//o_file_fd = fopen(fname1,"w+b");
	
		//flock(o_file_fd, LOCK_EX);
		//printf("soc_termファイルをロックしました\n");
			
	}
	if(strcmp(port, "54320") == 0){

		//o_file_fd = open(outputfile2, O_WRONLY | O_APPEND);
		o_file_fd = open(fname2, O_CREAT | O_WRONLY | O_APPEND, S_IRWXU | S_IRWXO);
		//o_file_fd = fopen(fname2,"w+b");
		//flock(o_file_fd, LOCK_EX);
		//printf("soc_term2ファイルをロックしました\n");
				
	}

	
	/*　4. 接続の受付け　*/
	while (true) {
		int fd = accept_fd(listen_fd); //接続を受ける

		handle_telnet = have_handle_telnet_option;
		handle_telnet_codes(-1, NULL, NULL); /* Reset internal state */

		warnx("accepted fd %d", fd);
		set_tty_noncanonical(); // 端末設定
		serve_fd(fd, o_file_fd); // リダイレクト操作の実行
		if (close(fd))
			err(1, "close");
		fd = -1;
		restore_termios();
	}
	//flock(o_file_fd, LOCK_UN);//ロック解除
	close(o_file_fd);
	
}
