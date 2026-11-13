const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../services/phone_web/index.html'), 'utf8');
const script = html.split('  const timerWheels = [];')[1].split('  function remember(')[0];
function element() {
  return {attrs: {}, handlers: {}, scrollTop: 0, dataset: {}, style: {},
    appendChild(child) { this.firstElementChild = child; },
    setAttribute(key, value) { this.attrs[key] = value; },
    getAttribute(key) { return this.attrs[key]; },
    addEventListener(key, callback) { this.handlers[key] = callback; },
    replaceChildren(...rows) { this.rows = rows; },
    getBoundingClientRect() { return {height: 40}; }};
}
const fields = ['hours', 'minutes', 'seconds'].map(id => ({id, value: '0'}));
const wheels = Object.fromEntries(fields.map(field => ['wheel-' + field.id, element()]));
const context = vm.createContext({hours: fields[0], minutes: fields[1], seconds: fields[2], edited: false,
  document: {getElementById: id => wheels[id], createElement: element}});
vm.runInContext('const timerWheels = [];' + script, context);
const hour = wheels['wheel-hours'], minute = wheels['wheel-minutes'];
assert.equal(hour.rows.length, 24);
assert.equal(minute.rows.length, 60);
const key = (wheel, name) => wheel.handlers.keydown({key: name, preventDefault() {}});
key(hour, 'End');
assert.equal(fields[0].value, '23');
assert.equal(hour.scrollTop, 920);
key(hour, 'ArrowDown');
assert.equal(fields[0].value, '23');
key(hour, 'Home');
assert.equal(fields[0].value, '0');
minute.scrollTop = 59 * 40;
minute.handlers.scroll();
assert.equal(fields[1].value, '59');
assert.equal(minute.attrs['aria-valuenow'], '59');
assert.equal(minute.rows[59].attrs['aria-current'], 'true');
minute.scrollTop = 58.5 * 40;
minute.handlers.scroll();
assert.equal(minute.rows[59].firstElementChild.style.transform, 'scale(0.8375)');
assert.equal(minute.rows[58].firstElementChild.style.transform, 'scale(0.8375)');
minute.rows[12].handlers.click();
assert.equal(fields[1].value, '12');
assert.equal(minute.scrollTop, 480);
minute.setAttribute('aria-disabled', 'true');
key(minute, 'End');
assert.equal(fields[1].value, '12');
assert.equal(context.edited, true);
console.log('phone_timer_wheels_test: PASS');
