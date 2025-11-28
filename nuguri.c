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
#define MAX_STAGES 2
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
} Enemy;

typedef struct {
    int x, y;
    int collected;
} Coin;

// 전역 변수
char map[MAX_STAGES][MAP_HEIGHT][MAP_WIDTH + 1];
int player_x, player_y;
int stage = 0;
int score = 0;
int heart = 3; //생명력 3으로 초기화

// 플레이어 상태
int is_jumping = 0;
int velocity_y = 0;
int on_ladder = 0;

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

//생명력 카운트
void heart_discount(void){
    if (heart <= 0) {
        printf("Game Over!");
        printf("\n모든 생명을 소모하셨습니다.\n");
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

    char c = '\0';
    int game_over = 0;

    while (!game_over && stage < MAX_STAGES) {
        if (kbhit()) {
            c = getchar();
            if (c == 'q') {
                game_over = 1;
                continue;
            }
            if (c == '\x1b') {
                getchar(); // '['
                switch (getchar()) {
                    case 'A': c = 'w'; break; // Up
                    case 'B': c = 's'; break; // Down
                    case 'C': c = 'd'; break; // Right
                    case 'D': c = 'a'; break; // Left
                }
            }
        } else {
            c = '\0';
        }

        update_game(c);
        draw_game();
        usleep(90000);

        if (map[stage][player_y][player_x] == 'E') {
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
    disable_raw_mode();
    return 0;
}

// 맵 파일 로드
void load_maps(void) {
    FILE *file = fopen("map.txt", "r");
    if (!file) {
        perror("map.txt 파일을 열 수 없습니다.");
        exit(1);
    }
    int s = 0, r = 0;
    char line[MAP_WIDTH + 2]; // 버퍼 크기는 MAP_WIDTH에 따라 자동 조절됨
    while (s < MAX_STAGES && fgets(line, sizeof(line), file)) {
        if ((line[0] == '\n' || line[0] == '\r') && r > 0) {
            s++;
            r = 0;
            continue;
        }
        if (r < MAP_HEIGHT) {
            line[strcspn(line, "\n\r")] = 0;
            strncpy(map[s][r], line, MAP_WIDTH + 1);
            r++;
        }
    }
    fclose(file);
}


// 현재 스테이지 초기화
void init_stage(void) {
    enemy_count = 0;
    coin_count = 0;
    is_jumping = 0;
    velocity_y = 0;

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            char cell = map[stage][y][x];
            if (cell == 'S') {
                player_x = x;
                player_y = y;
            } else if (cell == 'X' && enemy_count < MAX_ENEMIES) {
                enemies[enemy_count] = (Enemy){x, y, (rand() % 2) * 2 - 1};
                enemy_count++;
            } else if (cell == 'C' && coin_count < MAX_COINS) {
                coins[coin_count++] = (Coin){x, y, 0};
            }
        }
    }
}

// 게임 화면 그리기
void draw_game(void) {
    printf("\x1b[2J\x1b[H");
    printf("Stage: %d | Score: %d | Heart: %d \n", stage + 1, score, heart);
    printf("조작: ← → (이동), ↑ ↓ (사다리), Space (점프), q (종료)\n");

    char display_map[MAP_HEIGHT][MAP_WIDTH + 1];
    for(int y=0; y < MAP_HEIGHT; y++) {
        for(int x=0; x < MAP_WIDTH; x++) {
            char cell = map[stage][y][x];
            if (cell == 'S' || cell == 'X' || cell == 'C') {
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
            printf("%c", display_map[y][x]);
        }
        printf("\n");
    }
}

// 게임 상태 업데이트
void update_game(char input) {
    move_player(input);
    move_enemies();
    check_collisions();
}

// 플레이어 이동 로직
void move_player(char input) {
    int next_x = player_x, next_y = player_y;
    char floor_tile = (player_y + 1 < MAP_HEIGHT) ? map[stage][player_y + 1][player_x] : '#';
    char current_tile = map[stage][player_y][player_x];

    on_ladder = (current_tile == 'H');

    switch (input) {
        case 'a': next_x--; break;
        case 'd': next_x++; break;
        case 'w': if (on_ladder) next_y--; break;
        case 's': if (on_ladder && (player_y + 1 < MAP_HEIGHT) && map[stage][player_y + 1][player_x] != '#') next_y++; break;
        case ' ':
            if (!is_jumping && (floor_tile == '#' || on_ladder)) {
                is_jumping = 1;
                velocity_y = -2;
            }
            break;
    }

    if (next_x >= 0 && next_x < MAP_WIDTH && map[stage][player_y][next_x] != '#') player_x = next_x;
    
    if (on_ladder && (input == 'w' || input == 's')) {
        if(next_y >= 0 && next_y < MAP_HEIGHT && map[stage][next_y][player_x] != '#') {
            player_y = next_y;
            is_jumping = 0;
            velocity_y = 0;
        }
    } 
    else {
        if (is_jumping) {
            next_y = player_y + velocity_y;
            if(next_y < 0) next_y = 0;
            velocity_y++;

            if (velocity_y < 0 && next_y < MAP_HEIGHT && map[stage][next_y][player_x] == '#') {
                velocity_y = 0;
            } else if (next_y < MAP_HEIGHT) {
                player_y = next_y;
            }
            
            if ((player_y + 1 < MAP_HEIGHT) && map[stage][player_y + 1][player_x] == '#') {
                is_jumping = 0;
                velocity_y = 0;
            }
        } else {
            if (floor_tile != '#' && floor_tile != 'H') {
                 if (player_y + 1 < MAP_HEIGHT) player_y++;
                 else init_stage();
            }
        }
    }
    
    if (player_y >= MAP_HEIGHT) init_stage();
}


// 적 이동 로직
void move_enemies(void) {
    for (int i = 0; i < enemy_count; i++) {
        int next_x = enemies[i].x + enemies[i].dir;
        if (next_x < 0 || next_x >= MAP_WIDTH || map[stage][enemies[i].y][next_x] == '#' || (enemies[i].y + 1 < MAP_HEIGHT && map[stage][enemies[i].y + 1][next_x] == ' ')) {
            enemies[i].dir *= -1;
        } else {
            enemies[i].x = next_x;
        }
    }
}

// 충돌 감지 로직
void check_collisions(void) {
    for (int i = 0; i < enemy_count; i++) {
        if (player_x == enemies[i].x && player_y == enemies[i].y) {
            heart--; // 충돌시 생명 감소
            heart_discount(); //heart 수 계산하고 종료
            score = (score > 50) ? score - 50 : 0;
            init_stage();
            return;
        }
    }
    for (int i = 0; i < coin_count; i++) {
        if (!coins[i].collected && player_x == coins[i].x && player_y == coins[i].y) {
            coins[i].collected = 1;
            score += 20;
            play_sfx(); // 코인 획득시 효과음
        }
    }
}

