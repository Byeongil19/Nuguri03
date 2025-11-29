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

// SDL2 라이브러리는 모든 플랫폼에서 동일하게 포함
#include "SDL2/SDL.h"
#include "SDL2/SDL_mixer.h"

// 맵 및 게임 요소 정의 (수정된 부분)
#define MAP_WIDTH 40  // 맵 너비를 40으로 변경
#define MAP_HEIGHT 20
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

static Mix_Music *gMusic = NULL; //BGM 저장용 포인터

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
char map[MAX_STAGES][MAP_HEIGHT][MAP_WIDTH + 1]; // 왜 MAP_WIDTH는 +1을 하나요? 엔터로 칸을 구분하기 때문임
int player_x, player_y; //플래이어 2차원 위치
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
int kbhit(void);
void sfx_finished_callback(int); //효과음 메모리 해제 콜백
int init_sdl_mixer(void); //SDL_mixer 초기화
int play_bgm(int); //-1을 넣으면 무한 루프, 브금 함수
int play_sfx(void); //효과음 함수
void close_sdl_mixer(void); //오디오 종료 함수
void title_screen(void); // 시작 타이틀 함수
void ending_screen(int is_clear); // 엔딩 화면 함수
void print_file(const char* filename); // 텍스트 파일 출력함수
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

// 효과음 메모리 해제 콜백 함수
void sfx_finished_callback(int channel) {
    Mix_Chunk *chunk = Mix_GetChunk(channel);
    if (chunk != NULL) {
        Mix_FreeChunk(chunk);
    }
}

// SDL_mixer 초기화
int init_sdl_mixer(void) {
    //오류 처리
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        return 0;
    }

    int flags = MIX_INIT_MP3 | MIX_INIT_OGG;
    if (Mix_Init(flags) != flags) {
        return 0;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        return 0;
    }
    //실행
    Mix_ChannelFinished(sfx_finished_callback);
    Mix_VolumeMusic(MIX_MAX_VOLUME);
    Mix_Volume(-1, MIX_MAX_VOLUME);
    return 1;
}

// BGM 재생 함수 (무한 루프)
int play_bgm(int loop) {
    const char* BGM_PATH = "ponpoko_bgm.mp3";
    
    if (gMusic != NULL) { Mix_HaltMusic(); Mix_FreeMusic(gMusic); gMusic = NULL; }

    gMusic = Mix_LoadMUS(BGM_PATH);
    if (gMusic == NULL) {
        return 0;
    }

    if (Mix_PlayMusic(gMusic, loop) == -1) {
        return 0;
    }

    return 1;
}

// SFX 재생 함수 (호출시)
int play_sfx(void) {
    const char* SFX_PATH = "select_audio.wav";
    Mix_Chunk *current_sfx = Mix_LoadWAV(SFX_PATH);
    if (current_sfx == NULL) {
        return 0;
    }

    int channel = Mix_PlayChannel(-1, current_sfx, 0);

    if (channel == -1) {
        Mix_FreeChunk(current_sfx);
        return 0;
    }
    return 1;
}

// 오디오 종료 함수
void close_sdl_mixer(void) {
    if (gMusic != NULL) { Mix_FreeMusic(gMusic); gMusic = NULL; }
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
}


int main(void) {
    srand(time(NULL));
    enable_raw_mode();
    title_screen();
    load_maps();
    if (!init_sdl_mixer()) {
        printf("SDL_mixer 초기화 실패!\n");
    } 
    else { // BGM 실행
        if (!play_bgm(-1)) {
            printf("BGM 로드 실패!\n");
        }
    }
    init_stage();

    char c = '\0'; // 초기값 Null
    int game_over = 0;

    while (!game_over && stage < MAX_STAGES) {
        if (kbhit()) {
            c = getchar();
            if (c == 'c' ) { //초기화 테스트용 개발 로직
                init_stage();
                continue;
            }
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

        update_game(c); // 플래이어 이동-> 적 이동 -> 충돌감지
        draw_game(); //게임화면 그리기
        usleep(100000); // 테스트용 느린 프래임

        if (map[stage][player_y][player_x] == 'E') { //출구 도착
            stage++;
            score += 100;
            if (stage < MAX_STAGES) {
                init_stage();
            } else {
                game_over = 1;
                printf("\x1b[2J\x1b[H");
                printf("축하합니다! 모든 스테이지를 클리어했습니다!\n");
                printf("최종 점수: %d\n", score);
            }
        }
    }
    close_sdl_mixer(); // 오디오 종료
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
void load_maps() {
    FILE *file = fopen("map.txt", "r");
    if (!file) {
        perror("map.txt 파일을 열 수 없습니다.");
        exit(1);
    }
    int s = 0, r = 0; //s 는 스테이지 숫자 r는 맵 높이용
    char line[MAP_WIDTH + 2]; // 버퍼 크기는 MAP_WIDTH에 따라 자동 조절됨
    while (s < MAX_STAGES && fgets(line, sizeof(line), file)) {

        if (line[0] == '\n' || line[0] == '\r') { //멥 하나 로드가 끝났나요?
            //printf(" -> 빈 줄, s=%d r=%d 에서 스킵\n", s, r); 로직 확인용
            if(r >= MAP_HEIGHT) { // 맵 크기 세로가 20칸이고 다음 줄이 구분선이라고 가정함
                if (++s >= MAX_STAGES) { //혹시나 하고 넣은 맵 개수 제한선
                    break;
                }
                r = 0;
            }
            continue;
        }
        printf(" -> map[%d][%d] 에 저장: '%s'\n", s, r, line);
        if (r < MAP_HEIGHT) {
            line[strcspn(line, "\n\r")] = '\0';
            strncpy(map[s][r], line, MAP_WIDTH + 1);
            r++;
        }
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
            char cell = map[stage][y][x]; //y,x =높이, 너비 순임(입력과 출력 동일)
            if (cell == 'S') {
                player_x = x;
                player_y = y;
            } else if (cell == 'X' && enemy_count < MAX_ENEMIES) {
                if(map[stage][y+1][x] == '#' || map[stage][y+1][x] == 'H') {
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

// 게임 화면 그리기
void draw_game(void) {
    clrscr();
    printf("Stage: %d | Score: %d | Heart: %d \n", stage + 1, score, heart);
    printf("조작: ← → (이동), ↑ ↓ (사다리), Space (점프), q (종료)\n");

    char display_map[MAP_HEIGHT][MAP_WIDTH + 1];
    for(int y=0; y < MAP_HEIGHT; y++) {
        for(int x=0; x < MAP_WIDTH; x++) {
            char cell = map[stage][y][x];
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
    char floor_tile = (player_y + 1 < MAP_HEIGHT) ? map[stage][player_y + 1][player_x] : '#'; //발밑 블럭 확인용 맵 데이터에서 읽을 수 있는 범위인지 확인 -> 아니라면 '#'으로 취급
    char current_tile = map[stage][player_y][player_x]; //지금 위치(추측)

    on_ladder = (current_tile == 'H');
    printf("[before] key=%c, last key=%c, py=%d ny=%d floor='%c' on_ladder=%d jumpower'%d' below='%c'\n",
       input, last_input, player_y, next_y, floor_tile, on_ladder, velocity_y,
       map[stage][player_y + 1][player_x]); // 입력 확인용
    printf("%c\n", floor_tile);
    switch (input) { //입력에 따라서 새로운 좌표 생성
        case 'a': next_x--; break;
        case 'd': next_x++; break;
        case 'w': if (on_ladder) next_y--; break;
        case 's': if (((on_ladder && map[stage][player_y + 1][player_x] != '#') || (floor_tile == 'H')) && (player_y + 1 < MAP_HEIGHT)) next_y++; break; // 사다리 + 발밑 확인후 가능하면 이동
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
        if(next_y >= 0 && next_y < MAP_HEIGHT && map[stage][next_y][player_x] != '#') {
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
            else { //아니면 떨어짐
                next_y = player_y + 1;

            }
            if(input == ' ' || input == '\0') { // 자연스러운 점프 로직 -> 왜 앞으로 가다가 점프할 때는 제자리 점프만 하는가? (제가 테스트 할 때는 문제가 없었습니다!)
                switch (last_input) {
                    case 'a': next_x--; break;
                    case 'd': next_x++; break;
                    break;
                }
            }
            else
                last_input = '\0';
            if(next_y < 0) next_y = 0; //점프했는데 하늘에 머리박음
                

            if (velocity_y < 0 && next_y < MAP_HEIGHT && map[stage][next_y][player_x] == '#') { //점프했는데 천장에 머리박음
                velocity_y = 0;
            } else if (next_y < MAP_HEIGHT) {
                player_y = next_y;
            }
            
            if ((player_y + 1 < MAP_HEIGHT) && (map[stage][player_y + 1][player_x] == '#'||( !on_ladder && map[stage][player_y + 1][player_x] == 'H'))) { // 땅에 착지함
                is_jumping = 0;
                velocity_y = 0;
                if(map[stage][player_y + 1][next_x] == ' '){ //점프 하강 대각선 시 예외처리
                    is_jumping = 1;
                    velocity_y = -1;
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
    if (next_x >= 0 && next_x < MAP_WIDTH && map[stage][player_y][next_x] != '#') player_x = next_x;
    if (player_y >= MAP_HEIGHT) init_stage(); //맵 탈출 시 높이 버전
    if (player_x >= MAP_WIDTH-1) init_stage(); //맵 탈출 시 너비 버전
}


// 적 이동 로직
void move_enemies() {
    for (int i = 0; i < enemy_count; i++) { //dir은 이동속도 만약 음수를 곱하면 반대방향 이동
        int next_x = enemies[i].x + enemies[i].dir;
        if (next_x < 0 || next_x >= MAP_WIDTH || map[stage][enemies[i].y][next_x] == '#' || (enemies[i].y + 1 < MAP_HEIGHT && map[stage][enemies[i].y + 1][next_x] == ' ' && enemies[i].fly == 0)) {
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
            heart--; // 충돌시 생명 감소
            heart_discount(); //heart 수 계산하고 종료
            return;
        }
    }
    for (int i = 0; i < coin_count; i++) { //코인중 하나에 닿았나요?
        if (!coins[i].collected && player_x == coins[i].x && player_y == coins[i].y) { // 코인을 먹은적이 있나요? 코인과 같은 위치인가요?
            coins[i].collected = 1;
            score += 20;
            play_sfx(); // 코인 획득시 효과음
        }
    }
}
//맵 실행 및 출력 문제들
//실행 1차 시도 출력중 실패 맵 다운이 덜 받아졌거나 출력중 문제 생긴듯 -> 와 load_file if 조건문 순서 꼬아놨어 출력도 꼬임
//실행 2차 시도 맵 출력이 1줄씩 띄어짐 -> Q: 출력과정 문제인가? A: 아님 입력받은거 그대로 띄워줌 -> 파일 받을때 논리적 오류 발견 -> 조건문 추가로 해결
//실행 3차 시도 맵 출력시 맵 바닥에 다음 맵 천장이 붙어서 나옴 -> 일단 실행용으로 조건문 하드코딩시 실행가능하나 구조적 취약점 수정필요(수정됨)
//실행 4차 시도 하드코딩된 코드 재구축 -> 망할 엔터키로 줄 구분 및 스테이지 구분 혼용이 문제 -> 다시 로직 접근 -> 파일 형식 정확히 파악후 접근 -> 해결 성공 하지만 추후 가변 맵 생성시 장애물 될 것(아마도)



//실행 5차 시도 (중요 로직 테스트)
//플래이어 상승 하강시 대각선 이동 문제 -> 대각선 경로가 벽인데 왜 이동되지? -> 대각선 이동 구현이 필요해짐 혹은 x값과 y값이 만영되는 순서를 영리하게 정하던가
//정말 단순하게 구현된 점프 문제 -> 와 이게 점프? 그냥 위치값을 -2했다 다시 +2할 뿐이다. -> 점프력 구현 점프 자체는 잘 뜀 좌우 이동이 안되서 문제
//밑 칸이 사다리인데 내려갈 수 가 없다. -> 해당 로직 수정 문제 해결됨
//왜 밑 칸이 사다리인대 점프를 못하지? -> 로직 수정 문제 해결됨
//공중에 있는 적은 이동하지 못한다 -> ENEMY 구조체에 fly 변수 추가 맵 생성때 적 밑이 '#'가 아니라면 적은 날아다니는 것으로 간주합니다.

//수정된 오류들
//점프를 구현한 방법 점프키 인식했을때 점프력을 부여해서 그만큼 y갚을 한 프래임씩 움직이게 함 (점프시 매끄러운 좌 우 이동은 아직입니다!)
//점프시의 충돌감지 로직이 천장에 머리를 박은 것을 감지하지 못함 ->해결함 로직 오류였음

//생각만해도 머리 깨지는 오류들 (해결 안됨)
//그냥 하강시 대각선 이동 로직이 벽을 감지하지 못함 -> y값을 계산한 뒤 x값 반영함 -----잠깐만 그러면 대각선 이동을 구현한게 아닌데?
//점프시의 좌우 이동이 안됨 -> 입력 받을때 ㄱㄱㄱㄱㄱㄱㄱㄱㄱ누르다가 스패이스 누르면 기존 ㄱ 입력이 끊기죠? 같은 현상이었습니다... 와 이거 어케 해결함?

//코인 및 적 출구등등의 접촉은 이동 로직이 잘 되어 있다면 맛갈 일은 없습니다.(불확신)

//오류해결에 도움 되라고 넣은 로직들
//플레이어 이동 로직에다가 지금 각 변수값이 무엇인지 출력하게끔 만들어 놨습니다. q키를 눌러 멈췄을때 위로 올려 보시면 게임화면 위쪽에 출력이 되어 있습니다. 테스트용
//c를 누르면 맵이 초기화 되는 로직을 넣어 놨습니다 테스트할때 사용하고 제출할땐 지우세요.

//애매한 것
//사다리 위에서 점프한 뒤에 떨어지는 건 당연한 거지만 사다리 맨 위에서 점프한 뒤 떨어지는것도 당연한가? -> 일단은 맨 위에 착지하는 것으로 함

//가끔 키 인식이 씹히는 것 같은 느낌이 듭니다. 노트북 환경에서 작업해서 기기 문제일 수도 있지만 입력 버퍼의 고질적인 문제일 수도 있습니다.
//지연속도를 줄이면 게임이 굉장히 힘들어집니다.

//와 초기 맵 파일에다 장난을 쳐놨습니다. 일단은 맵 크기가 가로 40 세로 20칸이라고 가정하고 맵 다운받도록 만들겠습니다.

//테스트용 맵 파일이 있습니다. 출구도 쉬운 곳에다 배치해놨고 대충 깰수 있게끔 만들어놨습니다.
