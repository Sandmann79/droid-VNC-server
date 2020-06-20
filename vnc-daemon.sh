#!/system/bin/sh
WRITE_FOLD=/data/local/tmp
DAEMON_BIN=/system/bin/androidvncserver
PWD_FILE=/system/etc/avnc_pwd
PID_FILE=$WRITE_FOLD/vnc.pid
LOG_FILE=$WRITE_FOLD/vnc.log
LOGC="/system/bin/log -p v -t VNC-Daemon"

usage="Starts or Stops Android VNC Daemon
\n$0 [options] action
\n\tactions: start/stop/restart Daemon
\n\toptions: -s Scale (0-100%)
\n\t\t -m Method (flinger, gralloc, adb, framebuffer)"

scale="50"
method="flinger"
action="restart"

wait_mounted() {
    while [ ! -d $WRITE_FOLD ]; do
        $LOGC "Waiting for $WRITE_FOLD being available"
        sleep 1
    done
}

kill_server() {
    if [ -e $PID_FILE ]; then
        $LOGC "Killing PID $(cat $PID_FILE)"
        start-stop-daemon --stop --pidfile $PID_FILE
        rm -f $PID_FILE
    fi
}

start_server() {
    [ -e $PID_FILE ] && kill_server()
    [ -e $PWD_FILE ] && ARGS=-e $PWD_FILE
    $LOGC "Starting VNC-Daemon"
    touch $PID_FILE
    while [ -e $PID_FILE ]; do
        start-stop-daemon --start --make-pidfile \
        --pidfile $PID_FILE \
        --exec $DAEMON_BIN \
        -- -s $scale -m $method $ARGS
        sleep 1
    done
}

restart_server() {
    kill_server
    start_server
}

show_version() {
    $DAEMON_BIN -v
}

while getopts ":s:m:v" opt; do
    case $opt in
        s  ) scale=$OPTARG ;;
        m  ) method=$OPTARG ;;
        v  ) show_version
            exit 1 ;;
        \? ) echo "Unknown option!\n\n$usage"
            exit 1 ;;
    esac
done

shift $(($OPTIND - 1))
if [ -z "$@" ]; then
    echo $usage
    exit 1
else
    action=$@
fi

case $action in
    start   ) start_server ;;
    stop  ) kill_server ;;
    restart ) restart_server ;;
          * ) echo "Unknown action!\n\n$usage"
          exit 1 ;;
esac
