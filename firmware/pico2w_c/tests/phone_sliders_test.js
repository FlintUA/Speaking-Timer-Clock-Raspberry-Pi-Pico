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
    type: sliderNames.includes(match[1]) ? "range" : "text",
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
  volume_local: 12, volume_bt: 11, language: 0, audio_manual: 1, audio_hourly: 1,
  audio_timer: 1, audio_alarm: 1, light_mode: 1, light_day: 70, light_night: 20,
  editing: false, idle: true, bt_supported: true, saving: "saved", snapshot_id: 0
};
const alarm = {enabled: true, hour: 7, minute: 0, days: 31, sound: 0, track: 1, revision: 1};
const birthday = {enabled: true, month: 12, day: 10, hour: 7, minute: 0, birth_year: 0, revision: 1};
const state = {
  epoch: 123, command_id: 0, command_nonce: 0, command_result: "none", home_trusted: true,
  timer_running: false, timer_seconds: 60, player: "Closed", output: "LOCAL", settings: preferences,
  alarms: Array(5).fill(alarm), birthdays: Array(5).fill(birthday)
};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const posts = [], writes = [];
let lastBody = "", dropResponses = 0, rejectOnce = false, failStatusReads = 0;
const document = {getElementById, activeElement: null, addEventListener() {}};
const context = vm.createContext({
  Date, AbortController, document, setTimeout, clearTimeout,
  crypto: require("node:crypto").webcrypto, localStorage: {getItem: () => null},
  fetch: async (url, options) => {
    if (url === "/status.json") {
      if (failStatusReads) { --failStatusReads; throw new Error("Status temporarily unavailable"); }
      return {ok: true, json: async () => structuredClone(state)};
    }
    assert.equal(url, "/settings");
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
      preferences[fields[4]] = Number(fields[5]);
      preferences.snapshot_id = state.command_id;
      preferences.saving = "pending";
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
  console.log("phone_sliders_test: PASS");
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
}).finally(() => {
  vm.runInContext("clearTimeout(statusTimer); clearTimeout(sliderTimer);", context);
});
