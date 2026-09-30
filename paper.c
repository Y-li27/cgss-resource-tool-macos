#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<windows.h>
#include<conio.h>
#include"paper.h"


static HANDLE hOut;

void enable_vt(void){
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)){
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleOutputCP(CP_UTF8);   /* main.c と同じ: 出力を UTF-8 に固定する */
}

int console_rows(void){
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(hOut, &info)){
        return info.srWindow.Bottom - info.srWindow.Top + 1;
    }
    return 24;
}

int pager_pick(const char *title, def *Def,int multi){
    int count = 0;
    int sel = 0;
    int pager_row = console_rows() - 4;
    if(pager_row < 1) pager_row = 1;
    int n_state = 0;
    for(int i = 0;!(strcmp(Def[i].name,"END") == 0); i++){
        count++;
    }

    if(count < 1)
        return -2;
    printf("\x1b[?25l");
    printf("\x1b[2J\x1b[H");
    while(1){
        printf("\x1b[2J\x1b[H");
        int pager = sel / pager_row;
        int ps = pager * pager_row;
        int pe = ps + pager_row - 1;
        int total_pages = (count + pager_row - 1) / pager_row;
        if(pe > count -1) pe = count - 1;
        if(multi){
            printf("%s   [第%dページ/%dページ] 選択済%d ↑↓選択  Space選択  A全選択/全解除  PgUp/PgDnページ移動  Enter確定  Escキャンセル\n\n",title,pager + 1,total_pages,n_state);
        }
        else{
            printf("%s   [第%dページ/%dページ] ↑↓選択  PgUp/PgDnページ移動  Enter確定  Escキャンセル\n\n",title,pager + 1,total_pages);
        }
        for(int i = ps;i <= pe;i++){
            if(multi){
                if(sel == i)
                    printf("\x1b[7m%s %s\x1b[0m\n",Def[i].state ? "[X]":"[ ]",Def[i].name);
                else
                    printf("%s %s\n",Def[i].state ? "[x]":"[ ]",Def[i].name);
            }
            else{
                if(sel == i){
                    printf("\x1b[7m> %s\x1b[0m\n",Def[i].name);
                }
                else{
                    printf("  %s\n",Def[i].name);
                }
            }
        }

        int k = _getch();
        if (k < 0) continue;
        if(k == 0xE0 || k == 0){
            k = _getch();
            switch (k)
            {
            case 0x48:if(sel > 0) sel--;break;
            case 0x50:if(sel < count -1) sel++;break;
            case 0x49:sel -= pager_row;if(sel < 0) sel = 0;break;
            case 0x51:sel +=pager_row;if(sel > count -1) sel = count -1;break;
            case 0x47:sel = 0;break;
            case 0x4F:sel = count -1;break;
            }
        }
        else if(multi && (k == ' ' || k == 'A' || k == 'a')){
            if(k == ' '){
                Def[sel].state = !Def[sel].state;
                n_state += Def[sel].state ? 1:-1;
                if(sel < count -1)sel++;
            }
            else{
                if(k == 'a' || k == 'A'){
                    if(count == n_state){
                        for(int i = 0; i < count;i++){
                            Def[i].state = 0;
                        }
                        n_state = 0;
                    }
                    else{
                        for(int i = 0;i < count;i++){
                            Def[i].state = 1; 
                        }
                        n_state = count;
                    }
                }
            }
        }
        else if(k == '\r' || k == '\n' || k == 13){
            break;
        }
        else if(k == 27){
            sel = -1;
            break;
        }
    }
    console_cooked();
    printf("\x1b[?25h\x1b[2J\x1b[H");
    fflush(stdout);

    if(sel == -1){
        for(int i = 0;i < count;i++){
            Def[i].state = 0;
        }
        return -1;             /* Esc: 選択をクリアしてキャンセルを返す。先へ進まない */
    }
    if(!multi){
        if(Def[sel].func != NULL){
            Def[sel].func();
        }
        return sel;
    }
    return n_state;
}
/* sqlite3 を2つ受け取る関数と Def に対応*/
int pager_picks(const char *title, dbdef *Def,sqlite3 *db,sqlite3 *rdb,int multi){
    int count = 0;
    int sel = 0;
    int pager_row = console_rows() - 4;
    if(pager_row < 1) pager_row = 1;
    int n_state = 0;
    for(int i = 0;!(strcmp(Def[i].name,"END") == 0); i++){
        count++;
    }

    if(count < 1)
        return -2;
    printf("\x1b[?25l");
    printf("\x1b[2J\x1b[H");
    while(1){
        printf("\x1b[2J\x1b[H");
        int pager = sel / pager_row;
        int ps = pager * pager_row;
        int pe = ps + pager_row - 1;
        int total_pages = (count + pager_row - 1) / pager_row;
        if(pe > count -1) pe = count - 1;
        if(multi){
            printf("%s   [第%dページ/%dページ] 選択済%d ↑↓選択  Space選択  A全選択/全解除  PgUp/PgDnページ移動  Enter確定  Escキャンセル\n\n",title,pager + 1,total_pages,n_state);
        }
        else{
            printf("%s   [第%dページ/%dページ] ↑↓選択  PgUp/PgDnページ移動  Enter確定  Escキャンセル\n\n",title,pager + 1,total_pages);
        }
        for(int i = ps;i <= pe;i++){
            if(multi){
                if(sel == i)
                    printf("\x1b[7m%s %s\x1b[0m\n",Def[i].state ? "[X]":"[ ]",Def[i].name);
                else
                    printf("%s %s\n",Def[i].state ? "[x]":"[ ]",Def[i].name);
            }
            else{
                if(sel == i){
                    printf("\x1b[7m> %s\x1b[0m\n",Def[i].name);
                }
                else{
                    printf("  %s\n",Def[i].name);
                }
            }
        }

        int k = _getch();
        if (k < 0) continue;
        if(k == 0xE0 || k == 0){
            k = _getch();
            switch (k)
            {
            case 0x48:if(sel > 0) sel--;break;
            case 0x50:if(sel < count -1) sel++;break;
            case 0x49:sel -= pager_row;if(sel < 0) sel = 0;break;
            case 0x51:sel +=pager_row;if(sel > count -1) sel = count -1;break;
            case 0x47:sel = 0;break;
            case 0x4F:sel = count -1;break;
            }
        }
        else if(multi && (k == ' ' || k == 'A' || k == 'a')){
            if(k == ' '){
                Def[sel].state = !Def[sel].state;
                n_state += Def[sel].state ? 1:-1;
                if(sel < count -1)sel++;
            }
            else{
                if(k == 'a' || k == 'A'){
                    if(count == n_state){
                        for(int i = 0; i < count;i++){
                            Def[i].state = 0;
                        }
                        n_state = 0;
                    }
                    else{
                        for(int i = 0;i < count;i++){
                            Def[i].state = 1; 
                        }
                        n_state = count;
                    }
                }
            }
        }
        else if(k == '\r' || k == '\n' || k == 13){
            break;
        }
        else if(k == 27){
            sel = -1;
            break;
        }
    }
    console_cooked();
    printf("\x1b[?25h\x1b[2J\x1b[H");
    fflush(stdout);

    if(sel == -1){
        for(int i = 0;i < count;i++){
            Def[i].state = 0;
        }
        return -1;             /* Esc: 選択をクリアしてキャンセルを返す。先へ進まない */
    }
    if(!multi){
        if(Def[sel].func != NULL){
            Def[sel].func(db,rdb);
        }
        return sel;
    }
    return n_state;
}
// バージョンアップデート選択用の表示
int pager_pick_version(const char *title,versiondef *choice,double version,int multi){
    int count = 0;
    int sel = 0;
    int pager_row = console_rows() - 4;
    if(pager_row < 1) pager_row = 1;
    int n_state = 0;
    for(int i = 0;!(choice[i].version == (double)0.0); i++){
        count++;
    }

    if(count < 1)
        return -2;
    printf("\x1b[?25l");
    printf("\x1b[2J\x1b[H");
    while(1){
        printf("\x1b[2J\x1b[H");
        int pager = sel / pager_row;
        int ps = pager * pager_row;
        int pe = ps + pager_row - 1;
        int total_pages = (count + pager_row - 1) / pager_row;
        if(pe > count -1) pe = count - 1;
        if(multi){
            printf("%s   [第%dページ/%dページ] 選択済%d ↑↓選択  Space選択  A全選択/全解除  PgUp/PgDnページ移動  Enter確定  Escキャンセル\n\n",title,pager + 1,total_pages,n_state);
        }
        else{
            printf("%s   [第%dページ/%dページ] ↑↓選択  PgUp/PgDnページ移動  Enter確定  Escキャンセル\n\n",title,pager + 1,total_pages);
        }
        for(int i = ps;i <= pe;i++){
            if(multi){
                if(sel == i)
                    printf("\x1b[7m%s %.2lf\x1b[0m\n",choice[i].state ? "[X]":"[ ]",choice[i].version);
                else
                    printf("%s %.2lf\n",choice[i].state ? "[x]":"[ ]",choice[i].version);
            }
            else{
                if(sel == i){
                    printf("\x1b[7m> %.2lf\x1b[0m\n",choice[i].version);
                }
                else{
                    printf("  %.2lf\n",choice[i].version);
                }
            }
        }

        int k = _getch();
        if (k < 0) continue;
        if(k == 0xE0 || k == 0){
            k = _getch();
            switch (k)
            {
            case 0x48:if(sel > 0) sel--;break;
            case 0x50:if(sel < count -1) sel++;break;
            case 0x49:sel -= pager_row;if(sel < 0) sel = 0;break;
            case 0x51:sel +=pager_row;if(sel > count -1) sel = count -1;break;
            case 0x47:sel = 0;break;
            case 0x4F:sel = count -1;break;
            }
        }
        else if(multi && (k == ' ' || k == 'A' || k == 'a')){
            if(k == ' '){
                choice[sel].state = !choice[sel].state;
                n_state += choice[sel].state ? 1:-1;
                if(sel < count -1)sel++;
            }
            else{
                if(k == 'a' || k == 'A'){
                    if(count == n_state){
                        for(int i = 0; i < count;i++){
                            choice[i].state = 0;
                        }
                        n_state = 0;
                    }
                    else{
                        for(int i = 0;i < count;i++){
                            choice[i].state = 1; 
                        }
                        n_state = count;
                    }
                }
            }
        }
        else if(k == '\r' || k == '\n' || k == 13){
            break;
        }
        else if(k == 27){
            sel = -1;
            break;
        }
    }
    console_cooked();
    printf("\x1b[?25h\x1b[2J\x1b[H");
    fflush(stdout);

    if(sel == -1){
        for(int i = 0;i < count;i++){
            choice[i].state = 0;
        }
        return -1;             /* Esc: 選択をクリアしてキャンセルを返す。先へ進まない */
    }
    if(!multi){
        return sel;
    }
    return n_state;
}
