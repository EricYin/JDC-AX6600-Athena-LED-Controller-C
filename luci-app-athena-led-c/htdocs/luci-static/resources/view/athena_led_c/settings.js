'use strict';
'require view';
'require form';
'require fs';
'require ui';
'require poll';
'require network';

/*
 * Athena LED Controller (C) — LuCI JS Settings View
 * For JDCloud AX6600 (RE-CS-02) Pure C LED Daemon
 */

// Service status query: check PID file first, then verify cmdline, fallback to pidof
function getServiceStatus() {
	return fs.read('/var/run/athena-led-c.pid').then(function(pid) {
		pid = (pid || '').trim();
		if (pid && /^\d+$/.test(pid)) {
			return fs.read('/proc/' + pid + '/cmdline').then(function(cmdline) {
				if ((cmdline || '').indexOf('athena-led-c') !== -1)
					return { running: true, pid: pid };
				return checkPidof();
			}, function() {
				return checkPidof();
			});
		}
		return checkPidof();
	}).catch(function() {
		return checkPidof();
	});
}

function checkPidof() {
	return fs.exec_direct('/bin/pidof', ['athena-led-c']).then(function(res) {
		var pid = (res || '').trim().split(/\s+/)[0];
		if (pid && /^\d+$/.test(pid))
			return { running: true, pid: pid };
		return { running: false };
	}).catch(function() {
		return { running: false };
	});
}

function callInitAction(action) {
	return fs.exec('/etc/init.d/athena-led-c', [action]).then(function(res) {
		if (res.code !== 0)
			throw new Error(_('Command failed with code %d').format(res.code));
		ui.addNotification(null, E('p', _('Service action "%s" executed successfully.').format(action)), 'info');
	}).catch(function(e) {
		ui.addNotification(null, E('p', e.message), 'error');
	});
}

// 27×5 LED Matrix Preview Simulation
// Bit 0 = top row, bit 4 = bottom row
var CHAR_DICT = {
	'0': [31, 17, 31], '1': [17, 31, 16], '2': [25, 21, 19], '3': [17, 21, 31],
	'4': [7, 4, 31],   '5': [23, 21, 29], '6': [31, 21, 29], '7': [1, 1, 31],
	'8': [31, 21, 31], '9': [23, 21, 31],

	'A': [30, 5, 5, 30], 'B': [31, 21, 10], 'C': [14, 17, 17], 'D': [31, 17, 14],
	'E': [31, 21, 21], 'F': [31, 5, 5], 'G': [14, 17, 29], 'H': [31, 4, 31],
	'I': [17, 31, 17], 'J': [24, 16, 15], 'K': [31, 4, 27], 'L': [31, 16, 16],
	'M': [31, 2, 31], 'N': [31, 2, 4, 31], 'O': [14, 17, 14], 'P': [31, 5, 2],
	'Q': [14, 17, 30], 'R': [31, 5, 26], 'S': [18, 21, 9], 'T': [1, 31, 1],
	'U': [31, 16, 31], 'V': [15, 16, 15], 'W': [31, 8, 31], 'X': [27, 4, 27],
	'Y': [3, 28, 3], 'Z': [25, 21, 19],

	' ': [0], '-': [4, 4, 4], '_': [16, 16, 16], '=': [10, 10, 10],
	'+': [4, 14, 4], '*': [10, 4, 10], '/': [24, 6, 1], '\\': [1, 6, 24],
	'.': [16], ',': [16, 8], ':': [0, 10, 0], ';': [0, 0, 0], '^': [10],
	'℃': [3, 0, 14, 17, 17], '~': [8, 16, 8], '%': [25, 8, 19],
	'?': [1, 21, 3], '(': [14, 17], ')': [17, 14],
	'↑': [4, 14, 4, 4, 4], '↓': [4, 4, 4, 14, 4],

	'☀': [4, 21, 14, 21, 4], '☼': [17, 10, 4, 10, 17],
	'☂': [23, 3, 23, 3, 23], '☔': [11, 3, 11, 3, 11],
	'☁': [6, 15, 15, 6, 0], '⚡': [3, 6, 28, 8, 16]
};

function textToColumns(text) {
	var cols = [];
	Array.from(text || '').forEach(function(ch) {
		var glyph = CHAR_DICT[ch] || CHAR_DICT[ch.toUpperCase()];
		if (glyph) {
			cols = cols.concat(glyph);
			cols.push(0);
		}
	});
	if (cols.length)
		cols.pop();
	return cols;
}

function drawLedFrame(canvas, cols, scrollOffset) {
	var ctx = canvas.getContext('2d');
	var CELL = 12, DOT = 4.2, PAD = 10;
	var frame = new Array(27).fill(0);

	if (cols.length <= 27) {
		var off = Math.floor((27 - cols.length) / 2);
		for (var i = 0; i < cols.length; i++)
			frame[off + i] = cols[i];
	} else {
		for (var j = 0; j < 27; j++)
			frame[j] = cols[(scrollOffset + j) % (cols.length + 8)] || 0;
	}

	ctx.fillStyle = '#1a1a1a';
	ctx.fillRect(0, 0, canvas.width, canvas.height);

	for (var x = 0; x < 27; x++) {
		for (var y = 0; y < 5; y++) {
			var lit = (frame[x] >> y) & 1;
			ctx.beginPath();
			ctx.arc(PAD + x * CELL + CELL / 2, PAD + y * CELL + CELL / 2, DOT, 0, Math.PI * 2);
			ctx.fillStyle = lit ? '#ffb020' : '#332211';
			ctx.fill();
			if (lit) {
				ctx.beginPath();
				ctx.arc(PAD + x * CELL + CELL / 2, PAD + y * CELL + CELL / 2, DOT + 1.6, 0, Math.PI * 2);
				ctx.fillStyle = 'rgba(255,176,32,0.25)';
				ctx.fill();
			}
		}
	}
}

function buildPreviewBox() {
	var CELL = 12, PAD = 10;
	var canvas = E('canvas', {
		width: 27 * CELL + PAD * 2,
		height: 5 * CELL + PAD * 2,
		style: 'background:#1a1a1a;border-radius:8px;display:block;margin-top:10px'
	});

	var input = E('input', {
		type: 'text',
		class: 'cbi-input-text',
		style: 'max-width:320px;margin-right:10px',
		value: '12:34:56',
		placeholder: _('Type text to preview...')
	});

	var cols = textToColumns(input.value);
	var offset = 0;

	function updateCanvas(text) {
		input.value = text;
		cols = textToColumns(text);
		offset = 0;
		drawLedFrame(canvas, cols, 0);
	}

	input.addEventListener('input', function() {
		cols = textToColumns(input.value);
		offset = 0;
		drawLedFrame(canvas, cols, 0);
	});

	window.setInterval(function() {
		if (cols.length > 27) {
			offset = (offset + 1) % (cols.length + 8);
			drawLedFrame(canvas, cols, offset);
		}
	}, 128);

	drawLedFrame(canvas, cols, 0);

	var presets = [
		{ label: _('Time'), text: '12:34:56' },
		{ label: _('Date'), text: '2026-10-13' },
		{ label: _('Lunar'), text: 'L:9.4' },
		{ label: _('Sunrise/Sunset'), text: '06:12~18:45' },
		{ label: _('CPU'), text: 'C:12%' },
		{ label: _('Memory'), text: 'M:35%' },
		{ label: _('Temperature'), text: 'T:45.52' },
		{ label: _('Download'), text: 'D:12.5M' },
		{ label: _('Upload'), text: 'U:1.8M' }
	];

	var presetButtons = E('div', { style: 'margin-top:8px;display:flex;flex-wrap:wrap;gap:6px' },
		presets.map(function(p) {
			return E('button', {
				class: 'cbi-button cbi-button-neutral',
				style: 'padding:2px 8px;font-size:12px',
				click: function() { updateCanvas(p.text); }
			}, p.label + ' (' + p.text + ')');
		})
	);

	return E('div', { 'class': 'cbi-section' }, [
		E('h3', {}, _('Screen Preview')),
		E('div', { 'class': 'cbi-section-descr' },
			_('Simulates the 27×5 LED matrix display in real-time. Supports digits, A-Z letters and symbols with amber glow effect.')),
		E('div', { style: 'display:flex;align-items:center;flex-wrap:wrap' }, [
			input,
			E('span', { class: 'cbi-value-description' }, _('Click preset buttons below or type custom text'))
		]),
		presetButtons,
		canvas
	]);
}

function buildButtonHelpBox() {
	return E('div', { 'class': 'cbi-section' }, [
		E('h3', {}, _('Physical Button Gestures')),
		E('div', { 'class': 'cbi-section-descr' }, [
			E('p', { style: 'margin:4px 0' }, [
				E('strong', {}, _('Single Click: ')),
				_('Switch to next monitoring page immediately.')
			]),
			E('p', { style: 'margin:4px 0' }, [
				E('strong', {}, _('Double Click: ')),
				_('Jump directly back to the primary Time page.')
			]),
			E('p', { style: 'margin:4px 0' }, [
				E('strong', {}, _('Long Press (1.5s): ')),
				_('Toggle screen standby power (turn display ON or OFF).')
			])
		])
	]);
}

return view.extend({
	load: function() {
		return Promise.all([
			network.getDevices().catch(function() { return []; })
		]);
	},

	render: function(data) {
		var netDevices = data[0] || [];
		var m, s, o;

		m = new form.Map('athena-led-c', _('Athena LED Controller (C)'),
			_('Pure C11 LED daemon for JDCloud AX6600 (RE-CS-02). Ultra-low resource usage, hardware TM1628A driver with local monitor pages.'));

		// ============================================================
		// 板块 1: 基础设置
		// ============================================================
		s = m.section(form.NamedSection, 'main', 'main', _('General Settings'));
		s.addremove = false;

		o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.default = '1';
		o.rmempty = false;
		o.description = _('Enable or disable the Athena LED screen service.');

		o = s.option(form.ListValue, 'brightness', _('Brightness Level'));
		o.default = '5';
		for (var i = 0; i <= 7; i++) {
			var label = String(i);
			if (i === 0) label += _(' (Dimmest)');
			else if (i === 5) label += _(' (Default)');
			else if (i === 7) label += _(' (Brightest)');
			o.value(String(i), label);
		}
		o.description = _('Panel brightness level (0 to 7).');

		o = s.option(form.Value, 'page_secs', _('Page Display Time (seconds)'));
		o.datatype = 'range(1,60)';
		o.default = '5';
		o.description = _('Duration each monitor page stays on screen before advancing to the next page.');

		o = s.option(form.Value, 'net_interface', _('Traffic Monitor Interface'));
		o.default = 'br-lan';
		o.description = _('Network interface to monitor throughput for download and upload pages.');
		netDevices.forEach(function(dev) {
			var name = dev.getName();
			if (!/^(lo$|sit|gre|ifb|ip6|teql|erspan|miireg|phy)/.test(name))
				o.value(name);
		});

		o = s.option(form.Value, 'sun_coords', _('Coordinates for Sunrise/Sunset'));
		o.default = '39.90,116.40';
		o.placeholder = '39.90,116.40';
		o.value('39.90,116.40', _('Beijing (39.90, 116.40)'));
		o.value('31.23,121.47', _('Shanghai (31.23, 121.47)'));
		o.value('23.13,113.26', _('Guangzhou (23.13, 113.26)'));
		o.value('22.54,114.05', _('Shenzhen (22.54, 114.05)'));
		o.value('30.67,104.06', _('Chengdu (30.67, 104.06)'));
		o.value('30.59,114.31', _('Wuhan (30.59, 114.31)'));
		o.value('32.06,118.80', _('Nanjing (32.06, 118.80)'));
		o.value('30.27,120.15', _('Hangzhou (30.27, 120.15)'));
		o.value('34.26,108.94', _('Xi\'an (34.26, 108.94)'));
		o.description = _('Latitude and longitude ("lat,lon") used by the local solar algorithm to calculate sunrise and sunset times.');

		// ============================================================
		// 板块 2: 高级调优
		// ============================================================
		s = m.section(form.NamedSection, 'main', 'main', _('Advanced Tuning'));
		s.addremove = false;

		o = s.option(form.Value, 'scroll_ms', _('Scroll Interval (ms)'));
		o.datatype = 'range(50,500)';
		o.default = '128';
		o.description = _('Frame step interval in milliseconds for scrolling text that exceeds 27 columns (default 128ms).');

		o = s.option(form.Value, 'poll_ms', _('Sampling Interval (ms)'));
		o.datatype = 'range(100,1000)';
		o.default = '250';
		o.description = _('System sensor sampling and button polling interval in milliseconds (default 250ms).');

		// ============================================================
		// 板块 3: 服务操作
		// ============================================================
		s = m.section(form.NamedSection, 'main', 'main', _('Service Control'));
		s.addremove = false;

		o = s.option(form.Button, '_restart', _('Restart Service'));
		o.inputstyle = 'apply';
		o.inputtitle = _('Restart');
		o.onclick = function() { return callInitAction('restart'); };

		o = s.option(form.Button, '_stop', _('Stop Service'));
		o.inputstyle = 'remove';
		o.inputtitle = _('Stop');
		o.onclick = function() { return callInitAction('stop'); };

		o = s.option(form.Button, '_start', _('Start Service'));
		o.inputstyle = 'reload';
		o.inputtitle = _('Start');
		o.onclick = function() { return callInitAction('start'); };

		// ============================================================
		// 渲染视图与状态轮询
		// ============================================================
		return m.render().then(function(mapEl) {
			var statusText = E('em', {}, _('Collecting data...'));
			var statusBox = E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, _('Running Status')),
				E('div', { 'class': 'cbi-section-descr' }, [ statusText ])
			]);

			poll.add(function() {
				return getServiceStatus().then(function(st) {
					if (st.running) {
						statusText.innerHTML = '';
						statusText.appendChild(E('span', { 'style': 'color:green;font-weight:bold' }, _('RUNNING')));
						statusText.appendChild(document.createTextNode(' | PID: ' + st.pid));
					} else {
						statusText.innerHTML = '';
						statusText.appendChild(E('span', { 'style': 'color:red;font-weight:bold' }, _('NOT RUNNING')));
					}
				});
			}, 3);

			return E('div', {}, [
				statusBox,
				buildPreviewBox(),
				mapEl,
				buildButtonHelpBox()
			]);
		});
	}
});
