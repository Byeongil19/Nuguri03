#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>  //getch, kbhit
    #include <mmsystem.h>
#else
    #include <unistd.h>
    #include <termios.h>
    #include <fcntl.h>
#endif

// 맵 및 게임 요소 정의 (수정된 부분)
int MAP_WIDTH = 0; // 높이 너비를 변수로 변경 -> 동적 메모리 처리
int MAP_HEIGHT = 0; 
int current_stage = 0;
#define MAX_STAGES 3 // map.txt에 스테이지 추가할때 증가시킬것
#define MAX_ENEMIES 15 // 최대 적 개수 증가
#define MAX_COINS 30   // 최대 코인 개수 증가

#ifndef _WIN32
    struct termios orig_termios; // Windows에서는 사용하지 않음
#endif

#ifdef _WIN32
    // Windows: 더미 함수 (Raw 모드 제어가 필요 없으므로 빈 함수로 정의)
    void disable_raw_mode(void) { }
    void enable_raw_mode(void) { }

    // Windows: 키 입력 감지 함수 (kbhit)
    int kbhit(void) { return _kbhit(); }
    int save_space = 0;
#else
    // macOS/Linux (Unix) 환경
    void disable_raw_mode(void) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    void enable_raw_mode(void) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        atexit(disable_raw_mode);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }
    
    // macOS/Linux: 키 입력 감지 함수 (kbhit)
    int kbhit(void) {
        struct termios oldt, newt;
        int ch;
        int oldf;
        
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
        ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        fcntl(STDIN_FILENO, F_SETFL, oldf);
        
        if(ch != EOF) {
            ungetc(ch, stdin);
            return 1;
        }
        return 0;
    }
#endif // _WIN32

// 구조체 정의
typedef struct {
    int x, y;
    int dir; // 1: right, -1: left
    int fly;
} Enemy;

typedef struct {
    int x, y;
    int collected;
} Coin;

// 전역 변수
//char map[MAX_STAGES][MAP_HEIGHT][MAP_WIDTH + 1]; // 왜 MAP_WIDTH는 +1을 하나요? 엔터로 칸을 구분하기 때문임
int player_x, player_y; //플레이어 2차원 위치
int stage = 0;
int score = 0;
int heart = 3; //생명력 3으로 초기화

// 플레이어 상태
int is_jumping = 0;
int velocity_y = 0; //속도
int on_ladder = 0;
char last_input = '\0'; //전 키 기억용

// 게임 객체
Enemy enemies[MAX_ENEMIES];
int enemy_count = 0;
Coin coins[MAX_COINS];
int coin_count = 0;

// 함수 선언
void disable_raw_mode(void); // // 터미널 Raw 모드 활성화/비활성화
void enable_raw_mode(void); // 터미널 Raw 모드 활성화/비활성화
void load_maps(void); // 맵 파일 로드
void init_stage(void); // 현재 스테이지 초기화
void draw_game(void); // 게임 화면 그리기
void update_game(char input); // 게임 상태 업데이트
void move_player(char input); // 플레이어 이동 로직
void move_enemies(); // 적 이동 로직
void check_collisions(void); // 충돌 감지 로직
void heart_discount(void); // heart가 0 일때 종료 함수
void title_screen(void); // 시작 타이틀 함수
void ending_screen(int is_clear); // 엔딩 화면 함수
void print_file(const char* filename); // 텍스트 파일 출력함수
void beep_sound();

char **map = NULL; // map[y][x]

void free_map(void){
    if (!map) return;
    for (int y=0; y<MAP_HEIGHT; y++){
        free(map[y]);
    }
    free(map);
    map=NULL;
    MAP_WIDTH=0;
    MAP_HEIGHT=0;
}

void detect_size(){ //맵 가로 세로 길이 구하는 함수
    FILE *file = fopen("map.txt","r");
    if (!file){
        perror("map.txt 파일을 열 수 없습니다");
        exit(1);
    }
    char line[100];
    int w = 0; //
    int h = 0;

    while (fgets(line, sizeof(line), file)){ // 줄 읽기
        if (line[0]=='\n' || line[0]=='\r'){ // 만약 빈 줄이면
            if (h>0) { // 맵이 존재했으면
                if(current_stage==stage) break; //
                current_stage++;
                h=0;
                w=0;
            } // 빈줄이면 반복문 탈출 (한 txt에 저장된 맵들 구분용)
            continue;
        }
        int len=strcspn(line, "\n\r"); // line이 줄바꿈할때까지 읽어들임
        if(len>w) w=len; // 가로 길이 만큼 w에 저장
        h++; // 다음 행도 반복
    }
    fclose(file);
    MAP_WIDTH=w; // 전역변수에 대입
    MAP_HEIGHT=h;
}

void alloc_map(){
    map= (char**)malloc(sizeof(char*) * MAP_HEIGHT);
    if(!map){
        perror("map 메모리 할당 실패");
        exit(1);
    }
    for(int i=0;i<MAP_HEIGHT;i++){
        map[i]=(char*)malloc(sizeof(char)*(MAP_WIDTH+1));
        if (!map[i]){
            exit(1);
        }
    }
}

//화면 초기화
#ifdef _WIN32
    void clrscr() {
        system("cls");
    }

    void delay(int ms) {
        Sleep(ms); 
    }

#else
    void clrscr() {
        printf("\x1b[2J\x1b[H");
    }

    void delay(int ms){
        usleep(ms * 1000);
    }
#endif
// 시작, 엔딩 텍스트 출력 함수
void print_file(const char* filename){
    clrscr();
    FILE *file = fopen(filename, "r");
    if(!file){
        printf("%s 파일을 열 수 없습니다.\n", filename);
        return;
    }
    char line[200];
    while (fgets(line, sizeof(line), file)) {
        printf("%s", line);
    }
    fclose(file);
}
//생명력 카운트
void heart_discount(void){
    if (heart <= 0) {
        print_file("gameover.txt");
        disable_raw_mode();
        exit(0);
    }
}

int main(void) {
    #ifdef _WIN32 //윈도우용 화면 출력 깨짐 방지 콘솔 UTF-8 고정 화면출력
        system("chcp 65001> nul");
        printf("\x1b[?25l"); // 윈도우 환경에서 화면 깜빡임으로 인한 커서 숨기기
    #endif
    srand(time(NULL));
    enable_raw_mode();
    title_screen();
    stage=0;
    current_stage=0;
    detect_size();
    alloc_map();
    load_maps();
    init_stage();

    char c = '\0'; // 초기값 Null
    int game_over = 0;

    while (!game_over && stage < MAX_STAGES) {
        #ifdef _WIN32

        if (kbhit()) {
            c = getch();
            if (c == 'q') {
                game_over = 1;
                continue;
            }
            if (c == '\x1b') { //ESC를 입력 받았을 때 (이거 방향키 입력용)
                getch(); // '['
                switch (getch()) { // 점프 없다?
                    case 'A': c = 'w'; break; // Up
                    case 'B': c = 's'; break; // Down
                    case 'C': c = 'd'; break; // Right
                    case 'D': c = 'a'; break; // Left
                }
            }
            
        } else {
            c = '\0';
        }
        if (save_space) {
                save_space = 0;
                c = ' ';
            }
        #else
        if (kbhit()) {
            c = getchar();
            if (c == 'q') {
                game_over = 1;
                continue;
            }
            if (c == '\x1b') { //ESC를 입력 받았을 때 (이거 방향키 입력용)
                getchar(); // '['
                switch (getchar()) { // 점프 없다?
                    case 'A': c = 'w'; break; // Up
                    case 'B': c = 's'; break; // Down
                    case 'C': c = 'd'; break; // Right
                    case 'D': c = 'a'; break; // Left
                }
            }
        } else {
            c = '\0';
        }
        #endif

        update_game(c); // 플래이어 이동-> 적 이동 -> 충돌감지

        #ifdef _WIN32
        while (kbhit()) {
            char b = getch();
            if(b == ' ' && !is_jumping && !save_space) {
                save_space = 1;
                break; 
            }
        }// 키를 꾹 눌렀을 때 들어간 입력 버퍼 지우기 
        #else
        while (kbhit()) {
            char b = getchar();
            if(b == ' ' && !is_jumping) {
                ungetc(b, stdin);
                break; 
            }
        }// 키를 꾹 눌렀을 때 들어간 입력 버퍼 지우기 
        #endif
        draw_game(); //게임화면 그리기
        delay(80);

        if (map[player_y][player_x] == 'E') { //출구 도착
            stage++;
            score += 100;
            if (stage < MAX_STAGES) {
                free_map();
                current_stage=0;
                detect_size();
                alloc_map();
                load_maps();
                init_stage();
            } else {
                beep_sound();
                game_over = 1;
                printf("\x1b[2J\x1b[H");
                print_file("clear.txt");
                printf("\n최종 점수: %d\n", score);
            }
        }
    }
    #ifdef _WIN32
    printf("\x1b[?25h");    // Windows에서만 보이기
    #endif
    free_map();
    disable_raw_mode();//터미널 row 비활성화
    return 0;
}

// 시작 화면 출력
void title_screen(void){
    print_file("title.txt");
    printf("\n 아무 키나 누르면 시작합니다. \n");

    while(1){
        if(kbhit()){
            return;
        }
    }
}
void load_maps() { // 현재 진행중인 스테이지만 읽게끔 수정
    FILE *file = fopen("map.txt", "r");
    if (!file) {
        perror("map.txt 파일을 열 수 없습니다.");
        exit(1);
    }
    char line[100]; // 기존 line[width+2] 방법을 하기위해선 전제조건이 많아 수정
    int h=0; // 맵에서 현재 몇 번째 줄 읽고있는지 나타내는 변수
    int s = stage; // 불러올 스테이지
    current_stage=0;
    //원하는 스테이지 까지 건너뛰기
    while (current_stage<s && fgets(line, sizeof(line), file)){ //현재 스테이지 전까지 읽고 버림 (메모리 효율 고려)
        if (line[0] == '\n' || line[0] == '\r'){
            if(h>0){
                current_stage++;
                h=0; //스테이지 끝났으니까 h 초기화
            }
        }
        else {
            h++;
        }

    }
    h=0;

    // map[][]에 스테이지 내용 채워넣기 (기존 함수는 3차원 배열 방법이라 다시 짰음)
    while (h<MAP_HEIGHT && fgets(line, sizeof(line), file)){
        if (line[0]=='\n' || line[0]=='\r'){
            if (h>0) break; // 빈줄이면 종료
        }

        int len= strcspn(line, "\n\r");
        for(int x=0; x<MAP_WIDTH; x++){
            if(x<len) map[h][x] = line[x];
            else map[h][x]=' ';
        }
        map[h][MAP_WIDTH]= '\0';

        h++;
    }
    fclose(file);
    
}


// 현재 스테이지 초기화 -> 처음 시작할때, 플래이어가 죽었을때
void init_stage() {
    enemy_count = 0;
    coin_count = 0;
    is_jumping = 0;
    velocity_y = 0;
    on_ladder = 0; //이 줄은 없었지만 사다리 상태도 초기화 해야하지 않을까요? 혹시모름

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            char cell = map[y][x]; //y,x =높이, 너비 순임(입력과 출력 동일)
            if (cell == 'S') {
                player_x = x;
                player_y = y;
            } else if (cell == 'X' && enemy_count < MAX_ENEMIES) {
                if(map[y+1][x] == '#' || map[y+1][x] == 'H') {
                    enemies[enemy_count] = (Enemy){x, y, (rand() % 2) * 2 - 1, 0}; //이속 로직 dir 가능 값 -1 or 1 한 칸씩 이동함
                }
                else 
                    enemies[enemy_count] = (Enemy){x, y, (rand() % 2) * 2 - 1, 1};
                enemy_count++;
            } else if (cell == 'C' && coin_count < MAX_COINS) {
                coins[coin_count++] = (Coin){x, y, 0};
            }
        }
    }
}

void gotoxy(int x, int y) {
    printf("\033[%d;%dH", y, x);
}

// 게임 화면 그리기
void draw_game(void) {
    #ifdef _WIN32
    // Windows: 화면 지우지 말고 커서만 맨 위로
     gotoxy(1, 1); 
    #else
    // Linux/macOS: 기존 clrscr() 사용해도 깜빡임 거의 없음
    clrscr();
    #endif
    
    printf("Stage: %d | Score: %d | Heart: %d \n", stage + 1, score, heart);
    printf("조작: ← → (이동), ↑ ↓ (사다리), Space (점프), q (종료)\n");

    char display_map[MAP_HEIGHT][MAP_WIDTH + 1];
    for(int y=0; y < MAP_HEIGHT; y++) {
        for(int x=0; x < MAP_WIDTH; x++) {
            char cell = map[y][x];
            if (cell == 'S' || cell == 'X' || cell == 'C') { //플래이어, 적, 코인은 공백으로 둔다.
                display_map[y][x] = ' ';
            } else {
                display_map[y][x] = cell;
            }
        }
    }
    
    for (int i = 0; i < coin_count; i++) {
        if (!coins[i].collected) {
            display_map[coins[i].y][coins[i].x] = 'C';
        }
    }

    for (int i = 0; i < enemy_count; i++) {
        display_map[enemies[i].y][enemies[i].x] = 'X';
    }

    display_map[player_y][player_x] = 'P';

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for(int x=0; x< MAP_WIDTH; x++){
            if(display_map[y][x] == '\n')
                break;
            printf("%c", display_map[y][x]);
        }
        printf("\n");
    }
}

// 게임 상태 업데이트 플래이어 -> 적 -> 충돌여부 확인
void update_game(char input) {
    move_player(input);
    move_enemies();
    check_collisions();
}

// 플레이어 이동 로직
void move_player(char input) {
    int next_x = player_x, next_y = player_y; //기존 위치 저장
    char floor_tile = (player_y + 1 < MAP_HEIGHT) ? map[player_y + 1][player_x] : '#'; //발밑 블럭 확인용 맵 데이터에서 읽을 수 있는 범위인지 확인 -> 아니라면 '#'으로 취급
    char current_tile = map[player_y][player_x]; //지금 위치(추측)
    on_ladder = (current_tile == 'H');
    
    switch (input) { //입력에 따라서 새로운 좌표 생성
        case 'a': next_x--; break;
        case 'd': next_x++; break;
        case 'w': if (on_ladder) next_y--; break;
        case 's': if (((on_ladder && map[player_y + 1][player_x] != '#') || (floor_tile == 'H')) && (player_y + 1 < MAP_HEIGHT)) next_y++; break; // 사다리 + 발밑 확인후 가능하면 이동
        case ' ': //스페이스바
            if (!is_jumping && (floor_tile == '#' || on_ladder || floor_tile == 'H')) { // 점프 중이 아니고 밑 타일이 땅일때 or 사다리일때
                is_jumping = 1; //점프중 표현
                velocity_y = -2; //점프력 2
            }
            break;
    }
    
    if ((input == 'a' || input == 'd') && !is_jumping)
        last_input = input;
    else if (input == '\0' && !is_jumping)
        last_input = '\0';
    if ((on_ladder && (input == 'w' || input == 's')) || (floor_tile == 'H' && input == 's')) { //사다리 이동
        if(next_y >= 0 && next_y < MAP_HEIGHT && map[next_y][player_x] != '#') {
            player_y = next_y;
            is_jumping = 0;
            velocity_y = 0;
        }
    }
    else {
        if (is_jumping) { //점프 중 동작
            if (velocity_y < 0) {// 점프력이 0 보다 작으면
                next_y = player_y - 1;

            }
            else if(velocity_y ==0)
                next_y = player_y;
            else { //아니면 떨어짐
                next_y = player_y + 1;

            }
            if(input == ' ' || input == '\0') { // 자연스러운 점프 로직 -> 왜 앞으로 가다가 점프할 때는 제자리 점프만 하는가?
                switch (last_input) {
                    case 'a': next_x--; break;
                    case 'd': next_x++; break;
                    break;
                }
            }
            else
                last_input = '\0';
            if(next_y < 0) next_y = 0; //점프했는데 하늘에 머리박음

            if (velocity_y < 0 && next_y < MAP_HEIGHT && map[player_y][player_x] == '#') { //점프했는데 천장에 머리박음
                velocity_y = 0;
            } else if (next_y < MAP_HEIGHT && (map[next_y][player_x] != '#'||( !on_ladder && map[next_y][player_x] == 'H'))) {
                player_y = next_y;
            }
            
            if ((player_y + 1 < MAP_HEIGHT) && (map[player_y + 1][player_x] == '#'||( !on_ladder && map[player_y + 1][player_x] == 'H'))) { // 땅에 착지함
                is_jumping = 0;
                velocity_y = 0;
                if((map[player_y + 1][next_x] != '#' && map[player_y + 1][next_x] != 'H') && map[player_y][next_x] != '#'){ //점프 하강 대각선 시 예외처리
                    is_jumping = 1;
                    velocity_y = 1;
                }
            }
            velocity_y++; // 점프파워 감소
        } else { //점프중 아님 허공임
            if (floor_tile != '#' && floor_tile != 'H') {
                 if (player_y + 1 < MAP_HEIGHT) player_y++; //맵 안에서 떨어지고 있나요?
                 else init_stage(); // 구멍에 빠져서 맵 바깥으로 나갔을때
            }
        }
    }
    if (next_x >= 0 && next_x < MAP_WIDTH && map[player_y][next_x] != '#') player_x = next_x;
    if (player_y >= MAP_HEIGHT) init_stage(); //맵 탈출 시 높이 버전
    if (player_x >= MAP_WIDTH-1) init_stage(); //맵 탈출 시 너비 버전
}


// 적 이동 로직
void move_enemies() {
    for (int i = 0; i < enemy_count; i++) { //dir은 이동속도 만약 음수를 곱하면 반대방향 이동
        int next_x = enemies[i].x + enemies[i].dir;
        if (next_x < 0 || next_x >= MAP_WIDTH || map[enemies[i].y][next_x] == '#' || (enemies[i].y + 1 < MAP_HEIGHT && map[enemies[i].y + 1][next_x] == ' ' && enemies[i].fly == 0)) {
            enemies[i].dir *= -1;
        } 
        else {
            enemies[i].x = next_x;
        }
    }
}

// 충돌 감지 로직
void check_collisions(void) {
    for (int i = 0; i < enemy_count; i++) {
        if (player_x == enemies[i].x && player_y == enemies[i].y) { //적 중 하나에 닿았나요
            beep_sound();
            heart--; // 충돌시 생명 감소
            heart_discount(); //heart 수 계산하고 종료
            return;
        }
    }
    for (int i = 0; i < coin_count; i++) { //코인중 하나에 닿았나요?
        if (!coins[i].collected && player_x == coins[i].x && player_y == coins[i].y) { // 코인을 먹은적이 있나요? 코인과 같은 위치인가요?
            coins[i].collected = 1;
            score += 20;
            beep_sound();
        }
    }
}

//기본적인 시스템 비프음
void beep_sound(void){
    #ifdef _WIN32
        Beep(750, 120);
    #else
        printf("\a");
        fflush(stdout);
    #endif
}
