FPS profiler
============

A tiny profiling library for creating an execution summary report for the injected hotspots.

## Dependencies
There are no external dependencies.

## Usage
This project composed of two parts. One to integrate into a source code to setup hotspots and generate `raw event log` and another to convert this log into a human `readable` format.

### Integration
The following steps are required for the integration:
- Provide destination stream (or file) for the profiler `raw events`
- Setup one `frame-hotspot`
- Setup any other `hotspots` (optionally)

```cpp
#include <fpsprof/fpsprof.h>

void encode_single_frame(...)
{
    // Setup once on a top of the event stack
    FPSPROF_SCOPED_FRAME("encode_frame")
    {
        FPSPROF_SCOPED("block-0")
        ...
    }
    {
        FPSPROF_SCOPED("block-1")
        ...
    }
    ...
}

int main()
{
    // Any place before `main` function exit
    FPSPROF_SERIALIZE_STREAM(stdout) // or FPSPROF_SERIALIZE_FILE("fpsprof.log")
    ...
}
```
The `frame-hotspot` is a special type of `hotspot` which lets profiler to know the time of the frame processing start/end. This `hotspot` **must** be set only once before any other `hotspot` and the execution thread for this `hotspot` **must** not be changed within session.

#### Integration example
[x265](https://github.com/DmitryYudin/x265/commit/848eee09) - *Note that the default `x265` hotspots are for visualizing the timeline, and not for summary execution reports.*

### Report generation
First, build this project:
```bash
$ cmake -B out -S . && cmake --build out --config Release
```
On success, the report generator `fpsprof` will reside in the `out/Release` folder. When you can convert a `raw event log` to a `readable report`:
```bash
$ fpsprof fpsprof.log > report.txt
```

#### Report examples
* [dummy - single threaded](/report_example_simple.txt)
* [x265 - single threaded](/report_example_x265_st.txt)
* [x265 - multithreaded](/report_example_x265_mt.txt)
