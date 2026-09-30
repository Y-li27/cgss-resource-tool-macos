#include<stdbool.h>
#ifndef _DATA_H_
#define _DATA_H_

typedef struct 
{
    int id;
    char name[128];
    int bpm;
    char composer[64];
    bool music_exclude;
} musicInfo;

typedef struct
{
    long id;    //カードid
    char name[128];  //カード名
    int chara_id;   //キャラid
    int rarity;     //レアリティ
    int attribute;  //属性
    int title_flag; //称号カードフラグ    0=通常カード    1=呼び名カード
    int series_id;  //シリーズid
    long evolution_id;   //覚醒カードid
    int evolution_type;  //進化/カード種別
    int place;  //カードイラスト地点id
    int album_id;   //図鑑id
    int solo_live;  //Solo演出フラグ 非0ならあり 値=設定id
    int open_story_id;  //ストーリーid
    int open_dress_id;  //衣装id

}caraInfo;

#endif