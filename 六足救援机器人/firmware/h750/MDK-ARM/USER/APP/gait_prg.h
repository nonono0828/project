#ifndef GAIT_PRG_H
#define GAIT_PRG_H

#include "my_math.h"
#include "main.h"

#define LEG_LEN1 45.f
#define LEG_LEN2 66.f
#define LEG_LEN3 135.f

#define CHASSIS_LEN 162.2f        // 底盘长度（y轴方向）  （默认机器人的前方向为y轴，右方为x轴，上方为z轴）
#define CHASSIS_WIDTH 161.5f      // 底盘宽度（x轴方向）
#define CHASSIS_FRONT_WIDTH 93.3f // 底盘前端宽度（x轴方向）

#define N_POINTS 100 // 点的数量，也就是每秒的控制频率（必须是偶数）

/*腿的顺序
机器人的前方
4       1
5 俯视  2
6       3
*/
#define LEG1_THETA_STAND_1 PI / 4.f // 腿1站立时第一个关节的角度（不是对于电机来说的角度，请勿混淆）  
#define LEG2_THETA_STAND_1 0.f      // 腿2站立时第一个关节的角度
#define LEG3_THETA_STAND_1 -PI / 4.f
#define LEG4_THETA_STAND_1 3.f * PI / 4.f
#define LEG5_THETA_STAND_1 PI
#define LEG6_THETA_STAND_1 5.f * PI / 4.f
#define THETA_STAND_2 42.0f / 180.0f * PI // 机械腿站立时最后两个关节的角度
#define THETA_STAND_3 -130.0f / 180.0f * PI

#define K_CEN 500.0f     // 用于确定圆心模长的系数
#define KR_1 1           //%用于计算步伐大小的系数
#define KR_2 1.0f        //%用于计算步伐大小的系数
#define GAIT_LIFT_NORMAL_K  0.7f
#define GAIT_LIFT_OFFROAD_K (GAIT_LIFT_NORMAL_K * 3.5f)
#define MAX_R_PACE 60.0f // 最大步伐半径
#define MAX_SPEED 115.f  //最大速度
#define PACE_TIME_DEFALUT 1000 //默认情况下走一步需要的时间（单位ms）




// #define MAX_JOINT2_RAD PI / 2.0f           // 第2关节最大弧度 （由于总线舵机自带角度限制功能，因此这里已废弃，若使用pwm舵机可以自行重启）
// #define MIN_JOINT2_RAD -0.1f * PI          // 第2关节最小弧度
// #define MAX_JOINT3_RAD -(1.0f / 6.0f) * PI // 第3关节最大弧度
// #define MIN_JOINT3_RAD -(7.0f / 9.0f) * PI // 第3关节最小弧度

#define K_W (1.0f / 56.56854f) // 1/|B|_max

class Velocity
{
public:
    float Vx;    // x轴速度
    float Vy;    // y轴速度
    float omega; // 角速度
};

typedef struct
{
    Thetas thetas[N_POINTS];
} action;

typedef enum
{
    TRIPOD = 0, // 三角步态
    QUADPOD,    // 四角步态
    PENTAPOD,   // 五角步态
} gait_mode_e;

class Gait_prg
{
private:
    uint32_t pace_time;       // 走一步花费的时间
    Position3 Pws[6];         // 机械腿末端站立状态下相对于起始端的位置
    Position3 Pws_default[6]; // 默认情况下机械腿末端站立状态下相对于起始端的位置
    Position3 P_legs[6];      // 各个机械腿起始端相对于机器人中心的坐标
    Position3 CEN;            // 绕圆心的坐标
    float R_pace;             // 步伐大小（单位mm）
    Position3 body_pos;       // 机身位置
    Velocity velocity;        // 机身速度
    float lift_height_scale;  // 正常/越野模式抬腿高度系数
    Position3 hexapod_rotate(Position3 &point, uint32_t index);
    Position3 rotate_angle; // 机体旋转角度
    float move_point();

public:
    action actions[6];
    gait_mode_e gait_mode; // 步态模式
    void Init();           // 初始化
    void CEN_and_pace_cal();
    void gait_proggraming();
    uint32_t get_pace_time();
    void set_height(float height);
    void set_body_rotate_angle(Position3 &rotate_angle);
    void set_body_position(Position3 &body_pos);
    void set_velocity(Velocity &velocity);
    void set_offroad_mode(uint8_t enabled);
};

#endif
