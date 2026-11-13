const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const html = fs.readFileSync(path.join(__dirname, "../services/phone_web/index.html"), "utf8");
const source = html.split("<script>")[1].split("</script>")[0];
const sliderNames = ["volume_local", "volume_bt", "light_day", "light_night"];
const elements = new Map();

function createForm(id) {
  const markup = html.split('id="' + id + '"')[1].split("</form>")[0];
  const inputs = Array.from(markup.matchAll(/<(?:input|select)\b[^>]*\bname="([^"]+)"/g), match => ({
    name: match[1], value: match[1] === "slot" ? "1" : "", dataset: {}, checked: false,
    type: sliderNames.includes(match[1]) ? "range" : ["timer_voice","timer_random"].includes(match[1]) ? "checkbox" : "text",
    max: match[1].startsWith("volume_") ? "30" : "100", reportValidity: () => true
  }));
  const fields = Object.fromEntries(inputs.filter(field => field.name !== "days").map(field => [field.name, field]));
  const days = inputs.filter(field => field.name === "days");
  days.forEach((field, index) => { field.value = String(1 << index); });
  return {
    elements: fields, handlers: {}, querySelector: () => ({textContent: ""}), reportValidity: () => true,
    addEventListener(type, handler) { this.handlers[type] = handler; },
    querySelectorAll(selector) {
      if (selector === '[name="days"]') return days;
      if (id === "settings-controls" && selector === "button")
        return inputs.filter(field => field.type !== "range").map(field => ({dataset: {setting: field.name}}));
      return inputs;
    }
  };
}
for (const id of ["settings-controls", "alarm-controls", "birthday-controls"]) elements.set(id, createForm(id));
function getElementById(id) {
  if (!elements.has(id)) elements.set(id, {
    value: "0", dataset: {}, handlers: {}, querySelectorAll: () => [],
    addEventListener(type, handler) { this.handlers[type] = handler; }
  });
  return elements.get(id);
}

const preferences = {
  volume_local: 12, volume_bt: 11, language: 0, timer_voice:1,timer_sound:0,timer_track:1,timer_random:0, audio_manual: 1, audio_hourly: 1,
  audio_timer: 1, audio_alarm: 1, light_mode: 1, light_day: 70, light_night: 20,
  editing: false, idle: true, bt_supported: true, saving: "saved", snapshot_id: 0
};
const alarm = {enabled: true, hour: 7, minute: 0, days: 31, sound: 0, track: 1, revision: 1};
const birthday = {enabled: true, month: 12, day: 10, hour: 7, minute: 0, birth_year: 0, revision: 1};
const state = {
  firmware: {name: "Speaking Clock", board: "pico2_w", version: "1.2.3+4.gabcdef0", build: "abcdef012345-bt-mp3-wifi-phone", author: "FlintUA"},
  epoch: 123, command_id: 0, command_nonce: 0, command_result: "none", home_trusted: true,
  timer_running: false, timer_seconds: 60, player: "Closed", output: "LOCAL", settings: preferences,
  alarms: Array(5).fill(alarm), birthdays: Array(15).fill(birthday)
};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const posts = [], writes = [];
const controlCalls = [];
let nextControlResult = null;
let lastBody = "", dropResponses = 0, rejectOnce = false, failStatusReads = 0;
const quickButtons = ["sound", "say_time", "say_date", "next_alarm"].map((action,index) => {
  const button=getElementById(["sound-toggle","say-time","say-date","say-next-alarm"][index]);
  button.dataset.control=action;
  return button;
});
getElementById("quick-controls").querySelectorAll=()=>quickButtons;
const alarmLiveButtons=["alarm_stop","alarm_snooze","snooze_cancel"].map(action=>({dataset:{alarmLive:action}}));
getElementById("alarm-live-controls").querySelectorAll=()=>alarmLiveButtons;
const timerButtons=["set","start","cancel","stop_sound"].map(action=>action==="stop_sound"?
  getElementById("timer-sound-stop"):{dataset:{action}});
timerButtons[3].dataset.action="stop_sound";
getElementById("controls").querySelectorAll=()=>timerButtons;
getElementById("controls").reportValidity=()=>true;
const document = {getElementById, activeElement: null, addEventListener() {}};
const context = vm.createContext({
  Date, AbortController, document, setTimeout, clearTimeout,
  crypto: require("node:crypto").webcrypto, localStorage: {getItem: () => null},
  fetch: async (url, options) => {
    if (url === "/status.json") {
      if (failStatusReads) { --failStatusReads; throw new Error("Status temporarily unavailable"); }
      return {ok: true, json: async () => structuredClone(state)};
    }
    assert(["/settings", "/player", "/control", "/timer"].includes(url));
    posts.push(options.body);
    if (rejectOnce) {
      rejectOnce = false;
      ++state.command_id; // Another browser used the next command ID first.
      state.command_result = "applied";
      preferences.snapshot_id = state.command_id;
      return {ok: false, status: 409};
    }
    if (options.body !== lastBody) {
      const fields = options.body.split(" ");
      assert.equal(Number(fields[2]), state.command_id + 1);
      state.command_id = Number(fields[2]);
      state.command_nonce = Number(fields[3]);
      state.command_result = "applied";
      if (url === "/settings") preferences[fields[4]] = Number(fields[5]);
      if (url === "/player" && ["test_track", "test_stop"].includes(fields[4])) {
        state.player_test = fields[4] === "test_track";
        state.player = state.player_test ? "Playing" : "Closed";
        preferences.idle = !state.player_test;
      }
      if(url === "/timer" && fields[4]==="stop_sound") {
        if(nextControlResult) {state.command_result=nextControlResult;nextControlResult=null;}
        else if(Number(fields[5])!==state.overview.timer_sound_id) state.command_result="conflict";
        else state.overview.timer_sound_id=0;
      }
      if(url === "/control") {
        if(nextControlResult) {state.command_result=nextControlResult;nextControlResult=null;}
        else {
          controlCalls.push(fields[4]);
          if(fields[4]==="sound") state.overview.sound_on=Number(fields[5])===1;
          const a=state.overview.alarm;
          if(["alarm_stop","alarm_snooze","snooze_cancel"].includes(fields[4])) {
            const cancel=fields[4]==="snooze_cancel";
            if(Number(fields[5])!==(cancel?a.snooze_revision:a.active_id)) state.command_result="conflict";
            else if(cancel) {a.snoozes=0;++a.snooze_revision;}
            else {
              a.active=0;a.active_id=0;
              if(fields[4]==="alarm_snooze") {a.snoozes=1;a.snooze_seconds=180;++a.snooze_revision;}
            }
          }
        }
      }
      preferences.snapshot_id = state.command_id;
      if(url === "/settings") preferences.saving = "pending";
      writes.push({field: fields[4], value: Number(fields[5]), time: Date.now()});
      lastBody = options.body;
    }
    await wait(100); // Keep the request in flight while the next gesture arrives.
    if (dropResponses) { --dropResponses; throw new Error("Response lost after application"); }
    return {ok: true, status: 200};
  }
});
vm.runInContext(source, context);
const form = elements.get("settings-controls");
const update = () => vm.runInContext("update()", context);
function move(name, value, type = "input") {
  const field = form.elements[name];
  assert.equal(field.disabled, false);
  field.value = String(value);
  form.handlers[type]({target: field, type});
}
async function settled() {
  const deadline = Date.now() + 4000;
  while (Date.now() < deadline) {
    if (vm.runInContext("!busy && !pending && sliderChanges.size === 0", context)) return;
    await wait(25);
  }
  throw new Error("Latest slider values were not confirmed");
}

(async () => {
  await wait(20);
  assert.equal(form.elements.volume_local.value, "12");
  move("volume_local", 14);
  await wait(20);
  assert.equal(form.elements.volume_local.disabled, false);
  move("volume_local", 23);
  move("volume_local", 0, "change");
  await settled();
  assert.equal(preferences.volume_local, 0);
  assert.equal(preferences.volume_bt, 11);
  assert.equal(elements.get("value-volume_local").textContent, "0 / 30");
  assert(writes.filter(write => write.field === "volume_local").length <= 2);

  move("light_day", 85);
  move("light_night", 0, "change");
  await settled();
  assert.equal(preferences.light_day, 85);
  assert.equal(preferences.light_night, 0);
  assert.equal(elements.get("value-light_night").textContent, "0%");

  dropResponses = 2;
  move("volume_bt", 7);
  await wait(20);
  move("volume_bt", 26, "change");
  failStatusReads = 1;
  await settled();
  assert.equal(preferences.volume_bt, 26);
  assert.equal(preferences.volume_local, 0);
  assert(posts.some((body, index) => index && body === posts[index - 1]));
  rejectOnce = true;
  move("light_day", 43, "change");
  await settled();
  assert.equal(preferences.light_day, 43);
  for (let i = 1; i < writes.length; ++i) assert(writes[i].time - writes[i - 1].time >= 180);

  document.activeElement = form.elements.volume_local;
  preferences.volume_local = 19; // A later turn of the physical encoder.
  await update();
  assert.equal(form.elements.volume_local.value, "19");
  assert.equal(elements.get("value-volume_local").textContent, "19 / 30");

  const count = posts.length;
  move("language", 1);
  await wait(250);
  assert.equal(posts.length, count); // Other settings still require Apply.
  await form.handlers.click({target: {dataset: {setting: "language"}}});
  await settled();
  assert.equal(preferences.language, 1);
  preferences.editing = true;
  await update();
  assert(sliderNames.every(name => form.elements[name].disabled));
  preferences.editing = false;
  const play = getElementById("player-play");
  for (const [player, action, label] of [["Closed", "play", "Play"], ["Playing", "pause", "Pause"], ["Paused", "resume", "Play"]]) {
    state.player = player;
    await update();
    assert.equal(play.dataset.action, action);
    assert.equal(play.textContent, label);
  }
  state.player = "Closed";
  await update();
  const alarmForm = elements.get("alarm-controls");
  alarmForm.elements.track.value = "37";
  alarmForm.handlers.input({target: alarmForm.elements.track});
  assert.equal(vm.runInContext("scheduleEditors[0].dirty", context), true);
  await alarmForm.handlers.click({target: {dataset: {schedule: "test"}}});
  await settled();
  assert(posts.at(-1).endsWith("test_track 37"));
  assert.equal(elements.get("alarm-track-test").textContent, "Stop");
  alarmForm.elements.track.value = ""; // Stopping must not depend on track validation.
  await alarmForm.handlers.click({target: {dataset: {schedule: "test"}}});
  await settled();
  assert(posts.at(-1).endsWith("test_stop 0"));
  assert.equal(elements.get("alarm-track-test").textContent, "Test");
  state.player_test = true; // Audition started from another browser, including paused state.
  state.player = "Paused";
  await update();
  assert.equal(elements.get("alarm-track-test").textContent, "Stop");
  state.player_test = false; // Natural completion or physical Stop.
  state.player = "Closed";
  await update();
  assert.equal(elements.get("alarm-track-test").textContent, "Test");
  assert.equal(vm.runInContext("scheduleEditors[0].dirty", context), true);
  assert.equal(state.alarms[0].track, 1);
  for (const [name, value] of Object.entries(state.firmware))
    assert.equal(getElementById("about-" + name).textContent,
      name === "board" ? "Raspberry Pi Pico 2 W" : value);
  state.firmware.version = "1.2.4"; // A later status supplies the device's actual version.
  state.firmware.build = "fedcba987654-bt-mp3-wifi-phone";
  await update();
  assert.equal(getElementById("about-version").textContent, "1.2.4");
  assert.equal(getElementById("about-build").textContent, state.firmware.build);
  failStatusReads = 1;
  await update();
  assert.equal(getElementById("about-version").textContent, "Unavailable");
  await update();
  assert.equal(getElementById("about-version").textContent, "1.2.4");
  const firmware = state.firmware;
  delete state.firmware; // Older firmware has no metadata object.
  await update();
  assert.equal(getElementById("about-build").textContent, "Unavailable");
  state.firmware = firmware;
  await update();
  assert.equal(getElementById("about-build").textContent, firmware.build);
  state.overview={rtc_valid:true,date:"2026-11-13",weekday:5,sound_on:false,
    alarm:{active:2,deferred:1,next_slot:2,next_at:"2026-11-13T14:03:00",next_snoozed:true,
      snoozes:2,snooze_slot:2,snooze_seconds:179,snooze_waiting:false},
    bluetooth:{state:"connected",name:'Portable "test"'},network:{mode:"home",state:"ready",seconds:0},
    ntp:{available:true,configured:true,sampled:true,synced:false,pending:true,requesting:false}};
  await update();
  assert.equal(getElementById("date").textContent,"Fri · 13.11.2026");
  assert.equal(getElementById("sound-status").textContent,"Off");
  assert.equal(getElementById("next-alarm").textContent,"Alarm 2 · 13.11.2026 14:03 · snoozed");
  assert.equal(getElementById("alarm-status").textContent,"Alarm 2 ringing · 1 queued");
  assert(getElementById("snooze-status").textContent.includes("00:02:59"));
  assert.equal(getElementById("bluetooth-status").textContent,'Portable "test" · Connected');
  assert.equal(getElementById("ntp-status").textContent,"Clock update waiting for idle");
  state.overview.ntp.retry=true;
  await update();
  assert.equal(getElementById("ntp-status").textContent,"Clock update failed; retry pending");
  state.overview.ntp={available:true,configured:true,synced:true,sync_age:65};
  state.overview.alarm.snooze_waiting=true;
  await update();
  assert(getElementById("ntp-status").textContent.includes("00:01:05 ago"));
  assert(getElementById("snooze-status").textContent.includes("due, waiting"));
  state.overview.rtc_valid=false;
  state.overview.network.mode="direct";
  state.overview.ntp.synced=false;
  await update();
  assert.equal(getElementById("next-alarm").textContent,"Unavailable: clock date unknown");
  assert.equal(getElementById("ntp-status").textContent,"Direct mode · RTC only");
  failStatusReads=1;
  await update();
  assert.equal(getElementById("alarm-status").textContent,"Unavailable");
  await update();
  assert.equal(getElementById("alarm-status").textContent,"Alarm 2 ringing · 1 queued");
  delete state.overview;
  await update();
  assert.equal(getElementById("sound-status").textContent,"Unavailable");
  assert.equal(getElementById("quick-controls").hidden,true);
  state.overview={rtc_valid:true,date:"2026-11-13",weekday:5,sound_on:true,
    alarm:{active:0,next_slot:1,next_at:"2026-11-14T07:00:00",snoozes:0}};
  state.player="Closed"; state.timer_running=false;
  preferences.editing=false; preferences.idle=true; preferences.saving="saved";
  await update();
  const quick=getElementById("quick-controls");
  const clickControl=async index=>{await quick.handlers.click({target:quickButtons[index]});await settled();};
  const levels=[preferences.volume_local,preferences.volume_bt];
  assert.equal(quick.hidden,false);
  await clickControl(0);
  assert(posts.at(-1).endsWith("sound 0"));
  assert.equal(state.overview.sound_on,false);
  assert.equal(quickButtons[0].textContent,"Turn sound on");
  assert.equal(quickButtons[1].disabled,true);
  const mutedPosts=posts.length;
  await clickControl(1);
  assert.equal(posts.length,mutedPosts);
  await clickControl(0);
  assert(posts.at(-1).endsWith("sound 1"));
  assert.deepEqual([preferences.volume_local,preferences.volume_bt],levels);
  assert.equal(preferences.saving,"saved");
  assert.equal(getElementById("feedback").textContent,"Sound on.");
  for(let i=1;i<4;++i) await clickControl(i);
  assert.deepEqual(controlCalls.slice(-3),["say_time","say_date","next_alarm"]);
  assert.equal(getElementById("feedback").textContent,"Announcement requested.");
  dropResponses=2;
  const spoken=controlCalls.filter(action=>action==="say_time").length;
  await clickControl(1);
  await update();
  assert.equal(controlCalls.filter(action=>action==="say_time").length,spoken+1);
  assert.equal(posts.at(-1),posts.at(-2));
  nextControlResult="rtc_unavailable";
  await clickControl(2);
  assert.equal(getElementById("feedback").textContent,"Clock time/date is unavailable.");
  nextControlResult="no_alarm";
  await clickControl(3);
  assert.equal(getElementById("feedback").textContent,"No upcoming alarm to announce.");
  state.overview.alarm.active=2;
  await update();
  assert(quickButtons.every(button=>button.disabled));
  state.overview.alarm.active=0;
  preferences.editing=true;
  await update();
  assert.equal(quickButtons[0].disabled,false);
  assert.equal(quickButtons[1].disabled,true);
  preferences.editing=false; state.timer_running=true;
  await update();
  assert.equal(quickButtons[0].disabled,false);
  assert.equal(quickButtons[1].disabled,true);
  state.timer_running=false; state.player="Playing";
  await update();
  assert.equal(quickButtons[0].disabled,false);
  assert.equal(quickButtons[1].disabled,true);
  state.player="Closed"; state.overview.alarm.next_slot=0;
  await update();
  assert.equal(quickButtons[1].disabled,false);
  assert.equal(quickButtons[3].disabled,true);
  state.overview.rtc_valid=false;
  await update();
  assert.equal(quickButtons[0].disabled,false);
  assert.equal(quickButtons[1].disabled,true);
  state.home_trusted=false;
  await update();
  assert.equal(quick.hidden,true);
  assert(quickButtons.every(button=>button.disabled));
  const alarmLive=getElementById("alarm-live-controls");
  assert.equal(alarmLive.hidden,true);
  state.home_trusted=true;
  const a=state.overview.alarm;
  Object.assign(a,{active:5,active_id:70,snooze_revision:80,snoozes:0});
  state.overview.sound_on=false; // Alarm actions do not depend on speech/mute/editor/RTC guards.
  preferences.editing=true; state.timer_running=true;
  await update();
  assert.equal(alarmLive.hidden,false);
  assert.equal(alarmLiveButtons[0].disabled,false);
  assert.equal(alarmLiveButtons[1].disabled,false);
  assert.equal(alarmLiveButtons[2].disabled,true);
  const clickAlarm=async index=>{await alarmLive.handlers.click({target:alarmLiveButtons[index]});await settled();};
  dropResponses=1;
  await clickAlarm(1);
  assert(posts.at(-1).endsWith("alarm_snooze 70"));
  assert.equal(posts.at(-1),posts.at(-2));
  assert.equal(a.active,0);assert.equal(a.snooze_seconds,180);
  assert.equal(getElementById("feedback").textContent,"Alarm snoozed for 3 minutes.");
  assert.equal(alarmLiveButtons[0].disabled,true);
  assert.equal(alarmLiveButtons[2].disabled,false);
  assert(getElementById("alarm-live-hint").textContent.includes("saved alarm schedules"));
  const schedules=JSON.stringify(state.alarms);
  await clickAlarm(2);
  assert.equal(a.snoozes,0);
  assert.equal(JSON.stringify(state.alarms),schedules);
  assert.equal(preferences.saving,"saved");
  assert.equal(alarmLiveButtons[2].disabled,true);
  Object.assign(a,{active:5,active_id:71,snoozes:1,snooze_revision:83});
  await update(); // Browser still has occurrence 71 when another one starts.
  a.active_id=72;
  await clickAlarm(0);
  assert.equal(a.active,5);
  assert.equal(a.active_id,72);
  assert(getElementById("feedback").textContent.includes("State changed"));
  await clickAlarm(0);
  assert.equal(a.active,0);
  assert.equal(a.snoozes,1); // Stop does not cancel unrelated pending snoozes.
  assert.equal(getElementById("feedback").textContent,"Alarm stopped.");
  a.snooze_waiting=true; // A due repeat waiting for audio remains cancellable.
  await update();
  a.snooze_revision=84;
  await clickAlarm(2);
  assert.equal(a.snoozes,1);
  assert(getElementById("feedback").textContent.includes("State changed"));
  await clickAlarm(2);
  assert.equal(a.snoozes,0);
  failStatusReads=1;
  await update();
  assert.equal(alarmLive.hidden,true);
  assert(alarmLiveButtons.every(button=>button.disabled));
  await update();
  state.timer_running=false; preferences.editing=false; state.player="Closed";
  state.overview.timer_sound_id=100;
  await update();
  assert.equal(timerButtons[3].disabled,false);
  assert.equal(getElementById("timer").textContent,"Finished \u00b7 announcement playing");
  assert(alarmLiveButtons[0].disabled); // Countdown completion is not an alarm.
  const clickFinish=async()=>{await getElementById("controls").handlers.click({target:timerButtons[3]});await settled();};
  state.overview.timer_sound_id=101; // New finish event before an old page's click.
  await clickFinish();
  assert.equal(state.overview.timer_sound_id,101);
  assert(getElementById("feedback").textContent.includes("State changed"));
  dropResponses=1;
  await clickFinish();
  assert(posts.at(-1).endsWith("stop_sound 101"));
  assert.equal(posts.at(-1),posts.at(-2));
  assert.equal(state.overview.timer_sound_id,0);
  assert.equal(getElementById("feedback").textContent,"Timer sound stopped.");
  assert.equal(timerButtons[3].disabled,true);
  state.overview.timer_sound_id=102;
  await update();
  nextControlResult="no_timer_sound";
  await clickFinish();
  assert.equal(getElementById("feedback").textContent,"Timer completion audio has already ended.");
  delete state.overview.timer_sound_id;
  await update();
  assert.equal(timerButtons[3].disabled,true); // Older firmware.
  preferences.idle=true;preferences.editing=false;state.timer_running=false;
  await update();
  const voiceField=form.elements.timer_voice;
  assert.equal(voiceField.checked,true);
  voiceField.checked=false;
  form.handlers.change({target:voiceField,type:"change"});
  await form.handlers.click({target:{dataset:{setting:"timer_voice"}}});await settled();
  assert.equal(preferences.timer_voice,0);
  assert(posts.at(-1).endsWith("timer_voice 0"));
  move("timer_sound",2,"change");
  await form.handlers.click({target:{dataset:{setting:"timer_sound"}}});await settled();
  assert.equal(preferences.timer_sound,2);
  move("timer_track",255,"change");
  await form.handlers.click({target:{dataset:{setting:"timer_track"}}});await settled();
  assert.equal(preferences.timer_track,255);
  const randomField=form.elements.timer_random;
  randomField.checked=true;form.handlers.change({target:randomField,type:"change"});
  await form.handlers.click({target:{dataset:{setting:"timer_random"}}});await settled();
  assert.equal(preferences.timer_random,1);assert(form.elements.timer_track.disabled);
  randomField.checked=false;form.handlers.change({target:randomField,type:"change"});
  await form.handlers.click({target:{dataset:{setting:"timer_random"}}});await settled();
  assert.equal(preferences.timer_random,0);assert(!form.elements.timer_track.disabled);
  const randomAlarmForm=getElementById("alarm-controls");
  randomAlarmForm.elements.sound.value="2";
  randomAlarmForm.handlers.change({target:randomAlarmForm.elements.sound});
  await update();assert(randomAlarmForm.elements.track.disabled);
  randomAlarmForm.elements.sound.value="1";
  randomAlarmForm.handlers.change({target:randomAlarmForm.elements.sound});
  await update();assert(!randomAlarmForm.elements.track.disabled);
  preferences.idle=false;state.timer_running=true;
  await update();
  assert(voiceField.disabled && form.elements.timer_sound.disabled && form.elements.timer_track.disabled);
  preferences.idle=true;state.timer_running=false;
  delete preferences.timer_sound;delete preferences.timer_voice;delete preferences.timer_track;delete preferences.timer_random;
  await update();
  assert(voiceField.disabled && form.elements.timer_sound.disabled && form.elements.timer_track.disabled);
  console.log("phone_sliders_test: PASS");
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
}).finally(() => {
  vm.runInContext("clearTimeout(statusTimer); clearTimeout(sliderTimer);", context);
});
