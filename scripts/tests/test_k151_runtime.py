"""Run real K151 entry points with host platform objects, never hardware assertions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/m5stack/stackchan-k151"


def block(source, signature):
    start = source.index(signature)
    first = source.index("{", start)
    depth = 1
    end = first + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(shutil.which("g++"), "host C++ compiler required")
class K151RuntimeTests(unittest.TestCase):
    def run_cpp(self, source):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "check.cc"
            path.write_text(source)
            binary = Path(folder) / "check"
            subprocess.run(["g++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
                            "-I", str(BOARD), str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

    def test_actual_preview_and_theme_do_not_call_default_ui_or_wait_for_lvgl(self):
        source = (BOARD / "k151_display.cc").read_text()
        methods = "\n".join(block(source, name) for name in (
            "void K151Display::SetPreviewImage(", "void K151Display::SetTheme(",
            "void K151Display::ApplyFont(", "void K151Display::ApplyPreview("))
        self.run_cpp(r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
struct lv_font_t { int id; };
static lv_font_t lv_font_montserrat_16{0};
struct LvglFont {
    lv_font_t data; bool* destroyed;
    LvglFont(int id, bool* flag): data{id}, destroyed(flag) {}
    ~LvglFont() { *destroyed = true; }
    const lv_font_t* font() const { return &data; }
};
struct Theme {
    std::shared_ptr<LvglFont> text_font;
    std::shared_ptr<LvglFont> GetTextFont() { return text_font; }
};
struct ImageDesc { struct { unsigned w, h; } header; };
struct LvglImage {
    ImageDesc descriptor; bool* destroyed;
    LvglImage(unsigned w, unsigned h, bool* flag): descriptor{{w,h}}, destroyed(flag) {}
    ~LvglImage() { *destroyed = true; }
    ImageDesc* image_dsc() { return &descriptor; }
};
struct Object { bool hidden = true; const void* source = nullptr; unsigned scale=0; const lv_font_t* font=nullptr; };
constexpr int LV_OBJ_FLAG_HIDDEN=1;
static bool locked=false, old_font_destroyed=false;
void lv_obj_add_flag(Object* o, int) { assert(locked && o); o->hidden=true; }
void lv_obj_remove_flag(Object* o, int) { assert(locked && o); o->hidden=false; }
void lv_image_set_src(Object* o, const void* source) { assert(locked && o); o->source=source; }
void lv_image_set_scale(Object* o, unsigned scale) { assert(locked && o); o->scale=scale; }
void lv_obj_set_style_text_font(Object* o, const lv_font_t* font, int) {
    assert(locked && o && !old_font_destroyed); o->font=font;
}
struct Face { const lv_font_t* font=nullptr;
    void setSpeechTextFont(lv_font_t* f) { assert(locked); font=f; }
};
struct Robot { Face face; Face& avatar() { return face; } };
Robot& GetStackChan() { static Robot r; return r; }
struct SpiLcdDisplay {
    void SetPreviewImage(std::unique_ptr<LvglImage>) { assert(false && "default emoji_box does not exist"); }
};
struct DisplayLockGuard {
    explicit DisplayLockGuard(void*) { assert(false && "caller must not wait for LVGL"); }
};
struct K151Display: SpiLcdDisplay {
    std::mutex pending_mutex_; std::unique_ptr<LvglImage> pending_preview_, preview_image_cached_;
    std::shared_ptr<LvglFont> pending_font_, rendered_font_;
    bool preview_changed_=false, theme_changed_=false; Theme* current_theme_=nullptr;
    std::atomic<Theme*> requested_theme_{nullptr};
    Object image, status, notification, system;
    Object *preview_image_=&image, *status_label_=&status, *notification_label_=&notification, *system_label_=&system;
    uint64_t preview_until_=0;
    void SetPreviewImage(std::unique_ptr<LvglImage>); void SetTheme(Theme*);
    void ApplyFont(std::shared_ptr<LvglFont>); void ApplyPreview(std::unique_ptr<LvglImage>, uint64_t);
};
''' + methods + r'''
int main() {
    K151Display d;
    bool image_destroyed=false, new_font_destroyed=false;
    d.SetPreviewImage(std::make_unique<LvglImage>(640,480,&image_destroyed));
    assert(d.preview_changed_ && !image_destroyed && d.image.hidden);
    // A failed worker lock retains ownership; the caller never touched LVGL.
    auto queued=std::move(d.pending_preview_);
    assert(!image_destroyed && d.image.hidden);
    locked=true; d.ApplyPreview(std::move(queued), 100);
    assert(!d.image.hidden && d.image.scale==128 && d.preview_until_==10100);
    assert(d.image.source==d.preview_image_cached_->image_dsc() && !image_destroyed);
    d.ApplyPreview(nullptr, 10100);
    assert(d.image.hidden && d.image.source==nullptr && image_destroyed);
    locked=false;
    d.rendered_font_=std::make_shared<LvglFont>(1,&old_font_destroyed);
    Theme theme{std::make_shared<LvglFont>(2,&new_font_destroyed)};
    d.SetTheme(&theme);
    assert(d.theme_changed_ && d.requested_theme_.load()==&theme && !old_font_destroyed);
    auto next=d.pending_font_; theme.text_font.reset();
    assert(!new_font_destroyed && !old_font_destroyed);
    locked=true; d.ApplyFont(next);
    assert(old_font_destroyed && d.status.font==next->font() && d.system.font==next->font());
    assert(GetStackChan().avatar().font==next->font() && !new_font_destroyed);
}
''')

    def test_sleep_owner_handles_concurrent_atomic_requests_and_deadlines(self):
        self.run_cpp(r'''
#include <atomic>
#include <cassert>
#include <thread>
#include "k151_power_policy.h"
using Policy=k151::PowerPolicy;
int main() {
    Policy p(1,3); p.SetEnabled(true);
    assert(p.Poll(0,true)==Policy::None);
    assert(p.Poll(1000,true)==Policy::EnterSleep);
    p.WakeUp(); assert(p.Poll(2900,true)==Policy::ExitSleep);
    assert(p.Poll(3000,true)==Policy::None);
    assert(p.Poll(3900,true)==Policy::EnterSleep);
    assert(p.Poll(3950,false)==Policy::ExitSleep);
    assert(p.Poll(6949,true)==Policy::EnterSleep);
    assert(p.Poll(6950,true)==Policy::Shutdown);
    p.WakeUp(); assert(p.Poll(10000,true)==Policy::None);
    Policy concurrent(1,-1); concurrent.SetEnabled(true);
    std::atomic<bool> done{false};
    std::thread producer([&]{ for(int i=0;i<50000;++i) {
        concurrent.SetEnabled(i%2); concurrent.WakeUp();
    } done.store(true); });
    uint64_t now=uint64_t(UINT32_MAX)-1000;
    while(!done.load()) concurrent.Poll(++now,true);
    producer.join(); concurrent.SetEnabled(true); concurrent.WakeUp();
    concurrent.Poll(now,true);
    assert(concurrent.Poll(now+999,true)==Policy::None);
    assert(concurrent.Poll(now+1000,true)==Policy::EnterSleep);
    concurrent.SetEnabled(false); assert(concurrent.Poll(now+1001,true)==Policy::ExitSleep);
}
''')

    def test_actual_telemetry_accessors_are_cached_and_expire(self):
        source = (BOARD / "hal/hal.cc").read_text()
        methods = block(source, "bool Hal::ReadPower(") + "\n" + block(source, "bool Hal::ReadRtc(")
        self.run_cpp(r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <thread>
#include "k151_sensor_cache.h"
static std::atomic<int64_t> now_us{1000000};
int64_t esp_timer_get_time() { return now_us.load(); }
struct Hal {
    struct PowerSample { float volts,amps; };
    k151::SensorCache<PowerSample> power_cache_;
    k151::SensorCache<std::string> rtc_cache_;
    bool ReadPower(float&,float&); bool ReadRtc(std::string&);
};
''' + methods + r'''
int main() {
    Hal h; float volts=0,amps=0; std::string calendar;
    assert(!h.ReadRtc(calendar) && !h.ReadPower(volts,amps));
    h.rtc_cache_.Update("2026-10-13T12:00:00",1000);
    h.power_cache_.Update({4.1f,0.2f},1000);
    assert(h.ReadRtc(calendar) && calendar=="2026-10-13T12:00:00");
    assert(h.ReadPower(volts,amps) && volts==4.1f && amps==0.2f);
    now_us.store(6000000); assert(h.ReadRtc(calendar));
    now_us.store(6001000); assert(!h.ReadRtc(calendar) && !h.ReadPower(volts,amps));
    h.rtc_cache_.Invalidate(); assert(!h.ReadRtc(calendar));
    std::thread producer([&]{for(int i=0;i<10000;++i) h.rtc_cache_.Update("valid",6001);});
    for(int i=0;i<10000;++i) h.ReadRtc(calendar);
    producer.join(); assert(h.ReadRtc(calendar) && calendar=="valid");
}
''')
