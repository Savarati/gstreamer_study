main.c有一条预览管道和动态录像管道
main-1.c有添加了一条静态录像管道

编译：
gcc main.c -o main `pkg-config --cflags --libs gstreamer-1.0`

使用./main
videos 开始录像
videoq 结束录像
q 退出

