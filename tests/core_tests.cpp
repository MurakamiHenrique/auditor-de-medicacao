#include "auditor.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <random>
using namespace auditor;
static int checks=0;
#define CHECK(expr) do { ++checks; if(!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); return 1; } } while(0)

int main() {
    Settings s;
    CHECK(valid(s));
    Settings bad=s; bad.roi.w=0; CHECK(!valid(bad));
    bad=s; bad.roi.x=150; CHECK(!valid(bad));
    bad=s; bad.roi={0,0,W,H}; CHECK(!valid(bad));
    bad=s; bad.interval_s=0; CHECK(!valid(bad));
    bad=s; bad.confirmations=1; CHECK(!valid(bad));
    bad=s; bad.confirmations=100000; bad.interval_s=100000; CHECK(!valid(bad));
    bad=s; bad.window_s=5; CHECK(!valid(bad));
    bad=s; bad.margin=std::numeric_limits<float>::quiet_NaN(); CHECK(!valid(bad));
    bad=s; bad.hour=24; CHECK(!valid(bad));
    bad=s; bad.change=std::numeric_limits<float>::infinity(); CHECK(!valid(bad));
    bad=s; bad.change=4; CHECK(!valid(bad));
    bad=s; bad.min_cells=0; CHECK(!valid(bad));
    bad=s; bad.min_cells=N; CHECK(!valid(bad));
    std::array<uint8_t,W*H> img{};
    img.fill(100);
    Sample empty;
    CHECK(extract(img.data(),img.size(),s.roi,empty)); CHECK(empty.quality);
    CHECK(!extract(img.data(),img.size()-1,s.roi,empty));
    CHECK(!extract(nullptr,img.size(),s.roi,empty));
    Calibration refs; refs.empty=empty;
    CHECK(!calibrated(refs,s));
    refs.has_empty=true; CHECK(calibrated(refs,s));
    CHECK(cell_threshold(refs,s)==s.change);
    Calibration noisy=refs; noisy.noise=30; CHECK(cell_threshold(noisy,s)==30+s.margin);
    noisy.noise=std::numeric_limits<float>::quiet_NaN(); CHECK(!calibrated(noisy,s));
    auto low=refs; low.empty.quality=false; CHECK(!calibrated(low,s));
    CHECK(classify(empty,refs,s).label==Observation::Empty);
    CHECK(classify(empty,Calibration{},s).label==Observation::Unknown);

    // Any dose, anywhere: only the empty compartment is a reference (80x72 ROI at 40,24).
    auto scene=[&](std::initializer_list<std::array<int,5>> pills, int shift=0) {
        std::array<uint8_t,W*H> frame{}; frame.fill(uint8_t(100+shift));
        for(auto p:pills) for(int y=p[1];y<p[1]+p[3];++y) for(int x=p[0];x<p[0]+p[2];++x) frame[y*W+x]=uint8_t(p[4]+shift);
        Sample out; extract(frame.data(),frame.size(),s.roi,out); return out;
    };
    Sample pill=scene({{50,30,16,14,230}}); // small white pill, ~4% of the ROI
    auto r=classify(pill,refs,s);
    CHECK(r.label==Observation::Present); CHECK(r.change.blob>=30); CHECK(std::abs(r.change.offset)<1);
    CHECK(classify(scene({{95,70,16,14,230}}),refs,s).label==Observation::Present); // moved
    CHECK(classify(scene({{70,50,10,10,40}}),refs,s).label==Observation::Present);  // other, darker pill
    CHECK(classify(scene({{42,26,30,8,200}}),refs,s).label==Observation::Present);  // long capsule at the edge
    CHECK(classify(scene({{50,30,16,14,230},{90,60,12,12,180}}),refs,s).label==Observation::Present); // two pills
    CHECK(classify(scene({{70,50,2,2,230}}),refs,s).label==Observation::Empty); // crumb: one or two cells
    CHECK(classify(scene({{50,30,16,14,112}}),refs,s).label==Observation::Empty); // faint: below threshold
    CHECK(classify(scene({{50,30,16,14,112}}),refs,s).change.blob==0);
    noisy=refs; noisy.noise=150; // a dose below the calibrated noise is not trusted
    CHECK(classify(pill,noisy,s).label!=Observation::Present);

    // Light: a moderate global shift is compensated; a large one is unknown, never empty.
    CHECK(classify(scene({},15),refs,s).label==Observation::Empty);
    r=classify(scene({{50,30,16,14,215}},15),refs,s);
    CHECK(r.label==Observation::Present); CHECK(r.change.offset==15);
    CHECK(classify(scene({},30),refs,s).label==Observation::Unknown);
    // A change filling the whole ROI looks like light, not a dose: unknown.
    {
        std::array<uint8_t,W*H> frame{}; frame.fill(100);
        for(int y=s.roi.y;y<s.roi.y+s.roi.h;++y) for(int x=s.roi.x;x<s.roi.x+s.roi.w;++x) frame[y*W+x]=180;
        Sample full; CHECK(extract(frame.data(),frame.size(),s.roi,full));
        CHECK(classify(full,refs,s).label==Observation::Unknown);
    }
    // Scattered single cells (noise, not an object): neither present nor empty.
    auto unknown=empty; for(int i=0;i<N;i+=64) unknown.roi[i]=uint8_t(unknown.roi[i]+60);
    r=classify(unknown,refs,s);
    CHECK(r.label==Observation::Unknown); CHECK(r.change.blob==1 && r.change.cells==16);
    unknown=empty; unknown.quality=false;
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty; unknown.context.fill(255);
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty;
    for(int i=0;i<unknown.context_count;++i) unknown.context[i]+=21;
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);
    unknown=empty; unknown.context_count=0;
    CHECK(classify(unknown,refs,s).label==Observation::Unknown);

    // Sensor noise of +-6 levels: empty stays empty and the measured noise stays small.
    {
        std::mt19937 rng(7);
        std::array<uint8_t,W*H> frame{};
        for(auto &p:frame) p=uint8_t(100+int(rng()%13)-6);
        Sample grainy; CHECK(extract(frame.data(),frame.size(),s.roi,grainy));
        CHECK(noise_level(grainy.roi,empty.roi)<=6);
        CHECK(classify(grainy,refs,s).label==Observation::Empty);
        CHECK(noise_level(empty.roi,empty.roi)==0);
    }
    Sample black;
    img.fill(0); CHECK(extract(img.data(),img.size(),s.roi,black)); CHECK(!black.quality);
    img.fill(255); CHECK(extract(img.data(),img.size(),s.roi,black)); CHECK(!black.quality);
    // Valid ROI at image boundary remains in bounds; random image stress.
    Settings edge=s; edge.roi={128,88,32,32}; CHECK(valid(edge));
    std::mt19937 rng(42);
    for(int trial=0;trial<100;++trial) {
        for(auto &p:img) p=uint8_t(rng());
        CHECK(extract(img.data(),img.size(),edge.roi,black));
        auto random=classify(black,refs,edge); CHECK(std::isfinite(random.change.top));
        CHECK(random.change.blob<=random.change.cells && random.change.cells<=N);
    }

    // Daily schedule: offset to the next window start, resuming an open window.
    bad=s; bad.late_s=-1; CHECK(!valid(bad));
    bad=s; bad.window_s=43200; bad.late_s=43200; CHECK(!valid(bad));
    bad=s; bad.window_s=43201; bad.late_s=0; CHECK(!valid(bad));
    CHECK(next_window_offset(7*3600,s)==3600);
    CHECK(next_window_offset(8*3600,s)==0);
    CHECK(next_window_offset(8*3600+30,s)==-30);
    CHECK(next_window_offset(8*3600+60,s)==DAY_S-60); // Window closed: tomorrow.
    CHECK(next_window_offset(9*3600,s)==23*3600);
    Settings night=s; night.hour=23; night.minute=59; night.window_s=3600; CHECK(valid(night));
    CHECK(next_window_offset(1800,night)==-1860); // Window crossing midnight.
    CHECK(next_window_offset(23*3600,night)==59*60);
    CHECK(notifiable(State::Alert) && notifiable(State::NotLoaded) && !notifiable(State::Armed) && !notifiable(State::Idle));

    Cycle c;
    CHECK(c.finished(0));
    CHECK(!c.arm(0,1000,1000,s)); // Empty window.
    CHECK(!c.arm(2000,1000,61000,s)); // Start in the past.
    CHECK(c.arm(0,1000,61000,s));
    CHECK(!c.arm(0,1000,61000,s)); CHECK(!c.finished(0));
    CHECK(!c.needs_sample(999)); CHECK(c.needs_sample(1000));
    c.observe(999,Observation::Empty); CHECK(c.state==State::Armed);
    c.observe(1000,Observation::Present); CHECK(c.state==State::Monitoring && c.seen_present());
    CHECK(c.sampling(1001)); CHECK(!c.needs_sample(1001));
    c.observe(6000,Observation::Unknown);
    c.observe(11000,Observation::Empty); CHECK(c.state==State::Monitoring);
    c.observe(16000,Observation::Empty); CHECK(c.state==State::Removed);
    CHECK(c.finished(16000)); c.tick(999999); CHECK(c.state==State::Removed);

    // Empty from the start: never seen, so not a removal.
    CHECK(c.arm(20000,21000,81000,s));
    c.observe(21000,Observation::Empty); c.observe(26000,Observation::Empty);
    CHECK(c.state==State::NotLoaded && !c.seen_present());
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(0,Observation::Unknown); c.observe(5000,Observation::Empty); c.observe(10000,Observation::Empty);
    CHECK(c.state==State::NotLoaded); // Occlusion first is still no presence.
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(0,Observation::Empty); c.observe(5000,Observation::Present);
    c.observe(10000,Observation::Empty); c.observe(15000,Observation::Empty);
    CHECK(c.state==State::Removed); // A transient first empty is reset by presence.

    // Dose left in place: alert, then late removal during late_s.
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(55000,Observation::Present); c.tick(60000);
    CHECK(c.state==State::Alert); CHECK(c.sampling(60000)); CHECK(!c.finished(60000));
    CHECK(c.late_deadline()==60000+int64_t(s.late_s)*1000);
    CHECK(!c.arm(61000,62000,70000,s)); // No new window during late monitoring.
    CHECK(c.needs_sample(60000));
    c.observe(65000,Observation::Empty); CHECK(c.state==State::Alert);
    c.observe(70000,Observation::Empty); CHECK(c.state==State::LateRemoved && c.finished(70000));
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(55000,Observation::Present); c.tick(60000);
    c.observe(65000,Observation::Empty); c.observe(70000,Observation::Present); c.observe(75000,Observation::Empty);
    CHECK(c.state==State::Alert); // Late removal also needs persistence.
    int64_t late_end=c.late_deadline();
    CHECK(!c.needs_sample(late_end+1)); CHECK(c.finished(late_end+1));
    c.observe(late_end+1,Observation::Empty); c.observe(late_end+2,Observation::Empty);
    CHECK(c.state==State::Alert); // Expired: final alert, late captures ignored.
    CHECK(c.arm(late_end+10,late_end+10,late_end+70010,s)); CHECK(c.state==State::Armed);
    Settings strict=s; strict.late_s=0; CHECK(valid(strict));
    c.cancel(); CHECK(c.arm(0,0,60000,strict));
    c.observe(55000,Observation::Present); c.tick(60000);
    CHECK(c.state==State::Alert); CHECK(!c.needs_sample(60001)); CHECK(c.finished(60001));

    // Faults: no valid observation, stale data, late capture, occlusion at the end.
    c.cancel(); CHECK(c.arm(0,0,60000,s)); c.tick(60000); CHECK(c.state==State::Fault);
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(0,Observation::Present); c.tick(60000); CHECK(c.state==State::Fault); // Stale.
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(50000,Observation::Present); c.observe(55000,Observation::Empty); c.observe(60001,Observation::Empty);
    CHECK(c.state==State::Fault); // Capture completed after deadline.
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(50000,Observation::Present); c.observe(55000,Observation::Empty); c.observe(60000,Observation::Empty);
    c.tick(60000); CHECK(c.state==State::Removed); // Exact deadline accepted.
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(55000,Observation::Present); c.observe(60000,Observation::Unknown); c.tick(60000);
    CHECK(c.state==State::Fault); // Occlusion cannot become pending/removal.
    c.cancel(); CHECK(c.arm(0,0,60000,s));
    c.observe(0,Observation::Present); c.observe(5000,Observation::Empty); c.observe(20000,Observation::Empty);
    CHECK(c.state==State::Monitoring); // Gap breaks persistence.
    c.observe(25000,Observation::Empty); CHECK(c.state==State::Removed);
    c.cancel(); CHECK(c.state==State::Idle && c.finished(0));

    // Monotonic timestamps across 32-bit millisecond wrap and midnight.
    Cycle reboot; int64_t late=INT64_C(4294967290);
    CHECK(reboot.arm(late,late+10000,late+70000,s));
    reboot.observe(late+10000,Observation::Present);
    reboot.observe(late+15000,Observation::Empty); reboot.observe(late+20000,Observation::Empty);
    CHECK(reboot.state==State::Removed);
    std::printf("%d checks passed (synthetic software tests; not optical validation).\n",checks);
    return 0;
}
