set -eu

readonly input="--input FourPeople_1280x720_60.y4m"

entrypoint()
{
    local opt

    opt="" # test default option
    encode $opt

    opt="--pools 0 --frame-threads 1 --bframes 0"
    encode $opt

    opt="--pools 0 --frame-threads 2 --bframes 0 --rc-lookahead 0"
    encode $opt

    opt="--pools 0 --frame-threads 1 --bframes 0 --rc-lookahead 0 --preset superfast"
    encode $opt

    opt="--pools 0 --frame-threads 1 --bframes 0 --rc-lookahead 0 --preset superfast --ref 1"
    encode $opt

    opt="--pools 0 --frame-threads 1 --bframes 0 --rc-lookahead 0 --preset superfast --ref 1 --bitrate 2000"
    encode $opt
}

idx=0
header=
encode()
{
    if [ ! -f ./x265 ]; then
        echo "error: can't find x265 executable." >&2
        echo "https://github.com/DmitryYudin/x265/releases/download/v0.1/x265.exe" >&2
        return 1
    fi
    if ! [ -f ./FourPeople_1280x720_60.y4m ]; then
        echo "error: can't find input vector." >&2
        echo "https://media.xiph.org/video/derf/y4m/FourPeople_1280x720_60.y4m" >&2
        return 1
    fi
    if [ -z "$header" ]; then
        echo "input: $input"
        header=1
    fi
    echo "opt[$idx]: '$@'"
    set -- "$@" $input

    local dirLog=logs
    mkdir -p "$dirLog"

    local stdout=$dirLog/x265.$idx.log
    local stderr=$dirLog/report.$idx.log
    echo "$@" | tee $stdout > $stderr
    if ! ./x265 "$@" -o out.h265 >>$stdout 2>>$stderr; then
        cat $stderr
        return 1
    fi
    mv -f fpsprof.log $dirLog/fpsprof.$idx.log 
    rm -f out.h265
    idx=$((idx + 1))
}

entrypoint "$@"
