#include "http.h"
#include "util.h"

// 추가적으로 필요한 헤더 파일이 있다면 여기에 추가해 주세요.
#include <stdio.h>
#include <string.h>

#define PORT 8090 // 수정함
#define BACKLOG 10

Database g_Database;
int DoSomething(HttpClient* client, const char* request, char* response);

int main(int argc, char** argv) {
	int port = PORT;
	if (argc == 2) {
		if (sscanf(argv[1], "%d", &port) != 1 ||
			port < 0 || port > 65535) {

			printf("잘못된 포트입니다: %s\n", argv[1]);
			return 1;
		}
	} else if (argc >= 3) {
		printf("사용법: %s [포트]\n", argv[0]);
		return 1;
	}

	if (CreateDatabase(&g_Database) < 0) {
		printf("데이터베이스를 생성하지 못했습니다.\n");
		return 1;
	}

	printf("%d번 포트에서 서버를 엽니다.\n", port);

	HttpServer server;
	if (OpenHttpServer(&server, port, BACKLOG) < 0) {
		printf("서버를 열지 못했습니다.\n");
		return 1;
	}

	while (1) {
		HttpClient* client;
		AcceptHttpClient(&server, &client, &DoSomething);
	}
}

int DoSomething(HttpClient* client, const char* request, char* response) {
	/*
		아래에 있는 코드를 삭제하고, 여러분만의 코드를 작성해 주세요!

		request 매개변수에는 클라이언트가 보낸 HTTP 요청이 저장되어 있습니다. 널 문자를 포함하여 최대 2048바이트까지만 저장됩니다.
		response 매개변수에는 여러분이 보낼 HTTP 응답을 저장하면 됩니다. 널 문자를 포함하여 최대 2048바이트까지만 저장할 수 있습니다.
		(더 긴 길이의 요청/응답이 필요하다면 http.h 파일의 BUFFER_SIZE 매크로를 수정하면 됩니다.)

		이 함수의 리턴값의 의미는 다음과 같습니다:
		- 음수: 오류가 발생한 경우입니다. 클라이언트에 HTTP 응답을 보내지 않고 연결을 끊습니다.
		- 0: 클라이언트에 HTTP 응답을 보내고, 연결을 끊지 않고 대기합니다.
		- 양수: 클라이언트에 HTTP 응답을 보내고, 연결을 끊습니다.
	*/

	printf("클라이언트 %s가 요청한 내용:\n%s\n", client->AddressString, request);

	// Headers와 Body를 나누고 시작한다.
	char** headerAndBody;
	int headerAndBodyCount = SplitString(request, "\r\n\r\n", &headerAndBody);

	// Headers를, 한 줄씩 접근할 수 있도록 쪼갠다.
	char** headers;
	int headersCount = SplitString(headerAndBody[0], "\r\n", &headers);

	// Start line에 해당하는 headers[0]을 공백을 기준으로 나눈다.
	char** startLine;
	SplitString(headers[0], " ", &startLine);

	// HTTP/1.1이 아닌 경우이다. 505 코드로 처리한다.
	if (strcmp(startLine[2], "HTTP/1.1") != 0) {
		strcpy(response, "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 0\r\n\r\n");
		return 1;
	}

	// connection, cookie 헤더를 저장한다.
	char* connectionHeader = NULL;
	char* cookieHeader = NULL;

	// 0번은 start line이니까 1번부터 끝까지, 각 헤더를 돌면서
	for (int i = 1; i < headersCount; ++i) {
		// 각 헤더는 이름과 값으로 나뉜다.
		char** nameAndValue;
		// ": " 으로 이름과 값을 구분하여 저장한다.
		int nameAndValueCount = SplitString(headers[i], ": ", &nameAndValue);
		// 이름은 대소문자를 구분하지 않는다. 소문자로 저장하여 비교에 사용하자.
		for (int j = 0; j < strlen(nameAndValue[0]); ++j) {
			nameAndValue[0][j] = tolower(nameAndValue[0][j]);
		}
		// connection 헤더이다.
		if (strcmp(nameAndValue[0], "connection") == 0) {
			connectionHeader = nameAndValue[1];
			// connection 헤더의 값은 대소문자를 구분하지 않는다. 소문자로 저장하자.
			for (int j = 0; j < strlen(connectionHeader); ++j) {
				connectionHeader[j] = tolower(connectionHeader[j]);
			}
		} else if (strcmp(nameAndValue[0], "cookie") == 0) { // cookie 헤더이다.
			cookieHeader = nameAndValue[1]; // 얘는 나중에 또 쪼개야 한다.
		}
	}

	// cookie를 parsing
	char** cookies = NULL;
	int cookiesCount = 0;
	if (cookieHeader != NULL) { // cookie 헤더가 있을 때만,
	// split을 해준다.
		cookiesCount = SplitString(cookieHeader, "; ", &cookies);
	}

	// sessionid 라는 이름의 쿠키는 session ID를 가지고 있는 쿠키이다. 있다면 따로 저장해 준다.
	char* sessionIDCookie = NULL;
	if (cookies) {
		for (int i = 0; i < cookiesCount; ++i) { // cookies를 순회하면서
			char** nameAndValue;
			int nameAndValueCount = SplitString(cookies[i], "=", &nameAndValue); // 쿠키 이름과 값은 = 으로 구분되어 있다.

			if (strcmp(nameAndValue[0], "sessionid") == 0) { // sessionid를 찾으면 저장하고 break
				sessionIDCookie = nameAndValue[1];
				break;
			}
		}
	
	}

	// 세션이 유효한지 확인한다. 각 요청을 처리할 때 사용할 값이다.
	int isLoggedIn = !(sessionIDCookie ? CheckSession(&g_Database, sessionIDCookie) : -1);

	// GET /
	if (strcmp(startLine[0], "GET") == 0 && strcmp(startLine[1], "/") == 0) {
		if (isLoggedIn) { // 로그인된 경우 public/index.html 파일 전송
			char* indexHTML; // index.html 내용을 여기에 담는다.
			int indexHTMLLength = ReadTextFile("public/index.html", &indexHTML);

			// response 내용을 구성한다.
			strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: "); // Status line
			sprintf(response + strlen(response), "%d", indexHTMLLength);
			strcat(response, "\r\n\r\n"); // 개행 두 번으로 구분하고
			strcat(response, indexHTML); // index.html 내용
		} else { // 로그인이 되지 않은 경우 : /login으로 리다이렉트
			strcpy(response, "HTTP/1.1 302 Found\r\nLocation: /login\r\nContent-Length: 0\r\n\r\n");
		}
	}
	// GET /login
	else if (strcmp(startLine[0], "GET") == 0 && strcmp(startLine[1], "/login") == 0) {
		if (isLoggedIn) { // 로그인 되었으면 / 로 리다이렉트
			strcpy(response, "HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n\r\n");
		} else { // 아니면 이제 로그인을 한다.
			char* loginHTML; // login.html을 여기 담아서 전송할 것이다.
			int loginHTMLLength = ReadTextFile("public/login.html", &loginHTML);

			strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: "); // Status line
			sprintf(response + strlen(response), "%d", loginHTMLLength);
			strcat(response, "\r\n\r\n"); // 개행 2
			strcat(response, loginHTML); // login.html 내용
		}
	}
	// POST /login
	else if (strcmp(startLine[0], "POST") == 0 && strcmp(startLine[1], "/login") == 0) {
		if (isLoggedIn) { // 로그인되었다면: 잘못된 접근이다.
			strcpy(response, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
		} else { // 로그인되지 않았다면
			// body의 형식 : id=아이디&password=비밀번호

			char** body;
			// body를 id와 password로 쪼갠다.
			int bodyCount = SplitString(headerAndBody[1], "&", &body);

			char* id = NULL;
			char* password = NULL;

			for (int i = 0; i < bodyCount; ++i) {
				char** nameAndValue;
				// body의 각 항목(id pw)을 꺼내서 이름과 값으로 나눈다.
				int nameAndValueCount = SplitString(body[i], "=", &nameAndValue);

				// id password를 각각 다른 변수에 저장한다.
				if (strcmp(nameAndValue[0], "id") == 0) {
					id = nameAndValue[1];
				} else if (strcmp(nameAndValue[0], "password") == 0) {
					password = nameAndValue[1];
				}
			}

			// 이제 로그인을 시도하자.
			if (id != NULL && password != NULL) {
				char* sessionID;
				// 로그인을 시도한다.
				if (Login(&g_Database, id, password, &sessionID) == 0) { // 성공
					strcpy(response, "HTTP/1.1 302 Found\r\nLocation: /\r\nSet-Cookie: sessionid="); // Status line. / 로 리다이렉트.
					strcat(response, sessionID); // 로그인 성공해서 저장된 session ID이다.
					strcat(response, "\r\nContent-Length: 0\r\n\r\n");
				} else { // 실패하면 Bad Request
					strcpy(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
				}
			} else {
				strcpy(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
			}
		}
	}
	// GET /register
	else if (strcmp(startLine[0], "GET") == 0 && strcmp(startLine[1], "/register") == 0) {
		if (isLoggedIn) { // 로그인 되었다면 / 로 리다이렉트
			strcpy(response, "HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n\r\n");
		} else { // 로그인 안되었다면
			char* registerHTML; // register.html을 여기 담아서 보낸다.
			int registerHTMLLength = ReadTextFile("public/register.html", &registerHTML);
			// 위의 구조와 동일하게 보낸다. 내용은 register.html 의 내용이다.
			strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: ");
			sprintf(response + strlen(response), "%d", registerHTMLLength);
			strcat(response, "\r\n\r\n");
			strcat(response, registerHTML);
		}
	}
	// POST /register
	else if (strcmp(startLine[0], "POST") == 0 && strcmp(startLine[1], "/register") == 0) {
		if (isLoggedIn) { // 로그인된 상태라면 : 잘못된 접근이다.
			strcpy(response, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
		} else { // 로그인된 상태가 아니라면, 이제 회원 가입을 처리하자.
		// login과 같은 방식으로 parsing 한다.
			char** body;
			int bodyCount = SplitString(headerAndBody[1], "&", &body);

			char* id = NULL;
			char* password = NULL;

			for (int i = 0; i < bodyCount; ++i) {
				char** nameAndValue;
				int nameAndValueCount = SplitString(body[i], "=", &nameAndValue);

				if (strcmp(nameAndValue[0], "id") == 0) {
					id = nameAndValue[1];
				} else if (strcmp(nameAndValue[0], "password") == 0) {
					password = nameAndValue[1];
				}
			}

			// id pw parsing 끝

			// 이제 회원가입을 시도하자.
			if (id != NULL && password != NULL) { // 아이디 비번 둘 다 제대로 들어왔는가?
				if (Register(&g_Database, id, password) == 0) { // 회원가입 성공 : /login으로 리다이렉트 한다.
					strcpy(response, "HTTP/1.1 302 Found\r\nLocation: /login\r\nContent-Length: 0\r\n\r\n");
				} else { // 회원가입 실패 : bad request
					strcpy(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); // 
				}
			} else { // id pw가 제대로 입력되지 않았다면 bad request다.
				strcpy(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
			}
		}
	}
	// GET /text
	else if (strcmp(startLine[0], "GET") == 0 && strcmp(startLine[1], "/text") == 0) {
		if (isLoggedIn) { // 로그인 되었다면, DB에서 사용자가 저장한 text를 가져오자.
			const char* result = GetUserText(&g_Database, sessionIDCookie); // session ID를 제공해야 한다.

			strcpy(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "); // Status line
			sprintf(response + strlen(response), "%d", (int)strlen(result));
			strcat(response, "\r\n\r\n"); // 개행 2번 후
			strcat(response, result); // User가 저장해둔 text를 그대로 보낸다.
		} else { // 로그인이 필요한 접근인데 안됐다 -> Unauthorized
			strcpy(response, "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
		}
	}
	// POST /text
	else if (strcmp(startLine[0], "POST") == 0 && strcmp(startLine[1], "/text") == 0) {
		if (isLoggedIn) { // 로그인 되었다면, 사용자가 보낸 text를 DB에 넣자.
			SetUserText(&g_Database, sessionIDCookie, headerAndBody[1]); // 이 함수도 마찬가지로 session ID를 제공해야 한다.

			strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"); // OK!
		} else { // 로그인이 필요한 접근인데 안됐다 -> Unauthorized
			strcpy(response, "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
		}
	}
	// 그 외의 요청은 404로 처리하자.
	else {
		char* errorHTML; // 404.html의 내용을 담는다.
		int errorHTMLLength = ReadTextFile("public/404.html", &errorHTML);
		// 404 코드와 함께
		strcpy(response, "HTTP/1.1 404 Not Found\r\nContent-Length: ");
		sprintf(response + strlen(response), "%d", errorHTMLLength);
		strcat(response, "\r\n\r\n");
		strcat(response, errorHTML); // 404.html 내용을 보낸다.
	}

	if (connectionHeader != NULL) {
		// connection 헤더가 있는 경우에, 그 내용이 keep-alive라면
	 	if (strcmp(connectionHeader, "keep-alive") == 0) {
			// http.h에 정의된 TIMEOUT 만큼 클라이언트와의 연결을 유지한다.
			return 0;
		} else {
			// keep-alive가 아니면 바로 연결을 끊는다.
			return 1;
		}
	} else {
		// HTTP/1.1에서, connection 헤더가 없으면 기본적으로 연결을 끊는다.
		return 0;
	}


	// ---------------------------------------------------------------------
	// char* dynamicContent = "Hello, world!";

	// sprintf(response,
	// 	"HTTP/1.1 200 OK\r\n"
	// 	"Content-Length: %d\r\n"
	// 	"Content-Type: text/plain\r\n"
	// 	"\r\n"
	// 	"%s",
	// 	(int)strlen(dynamicContent), dynamicContent);

	// return 0;
}