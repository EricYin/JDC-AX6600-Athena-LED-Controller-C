'use strict';
'require view';
'require form';
'require fs';
'require ui';
'require uci';
'require poll';
'require network';

/*
 * Athena LED Controller (C) — LuCI JS Settings View
 * For JDCloud AX6600 (RE-CS-02) Pure C LED Daemon
 * Single-screen static display, direct checkbox selection with live preview
 */

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

// 27×5 LED Matrix Preview Simulation (Single Screen, Static Centered)
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
	'.': [16], ',': [16, 8], ':': [10], ';': [0, 0, 0], '^': [10],
	'℃': [3, 0, 14, 17, 17], '~': [8, 16, 8], '%': [25, 8, 19],
	'?': [1, 21, 3], '(': [14, 17], ')': [17, 14]
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

function drawLedFrame(canvas, cols) {
	var ctx = canvas.getContext('2d');
	var CELL = 12, DOT = 4.2, PAD = 10;
	var frame = new Array(27).fill(0);

	if (cols.length <= 27) {
		var off = Math.floor((27 - cols.length) / 2);
		for (var i = 0; i < cols.length; i++)
			frame[off + i] = cols[i];
	} else {
		for (var k = 0; k < 27; k++)
			frame[k] = cols[k] || 0;
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

// 7 个纯本地功能项（无日出日落）
var PAGE_ITEMS = [
	{ id: 'time',     label: _('Time'),        icon: '🕒', preview: '12:34:56' },
	{ id: 'date',     label: _('Date'),        icon: '📅', preview: '10-13' },
	{ id: 'lunar',    label: _('Lunar'),       icon: '🏮', preview: 'L:9.4' },
	{ id: 'cpu',      label: _('CPU'),         icon: '💻', preview: 'C:12%' },
	{ id: 'mem',      label: _('Memory'),      icon: '💾', preview: 'M:35%' },
	{ id: 'temp',     label: _('Temperature'), icon: '🌡️', preview: 'T:45.52' },
	{ id: 'net_down', label: _('Download'),    icon: '⬇️', preview: 'D:12.5M' },
	{ id: 'net_up',   label: _('Upload'),      icon: '⬆️', preview: 'U:1.8M' }
];

function buildButtonHelpBox() {
	return E('div', { 'class': 'cbi-section' }, [
		E('h3', {}, _('Physical Button Gestures')),
		E('div', { 'class': 'cbi-section-descr' }, [
			E('p', { style: 'margin:4px 0' }, [
				E('strong', {}, _('Single Click: ')),
				_('Manually flip to the next checked page in the rotation list.')
			]),
			E('p', { style: 'margin:4px 0' }, [
				E('strong', {}, _('Double Click: ')),
				_('Jump directly back to the first checked page.')
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
			network.getDevices().catch(function() { return []; }),
			uci.load('athena-led-c')
		]);
	},

	render: function(data) {
		var netDevices = data[0] || [];
		var m, s, o;

		m = new form.Map('athena-led-c', _('Athena LED Controller (C)'),
			_('Pure C11 LED daemon for JDCloud AX6600 (RE-CS-02). Single-screen static display, zero horizontal scrolling, lazy monitoring.'));

		// ============================================================
		// 板块 1: 屏幕点阵预览与显示功能勾选
		// ============================================================
		s = m.section(form.NamedSection, 'main', 'main', _('Screen Preview & Display Features'));
		s.addremove = false;

		var previewCanvas = null;
		function updatePreview(text) {
			if (previewCanvas) {
				var cols = textToColumns(text);
				drawLedFrame(previewCanvas, cols);
			}
		}

		// 核心：绑定 pages 选项，自定义渲染控件与提交逻辑
		o = s.option(form.DynamicList, 'pages', _('Display Pages'));
		o.rmempty = false;

		o.cfgvalue = function(section_id) {
			var val = uci.get('athena-led-c', section_id, 'pages');
			if (val == null)
				return ['time', 'temp', 'date'];
			return Array.isArray(val) ? val : [val];
		};

		o.renderWidget = function(section_id, option_index, cfgvalue) {
			var self = this;
			var selectedValues = Array.isArray(cfgvalue) ? cfgvalue : (cfgvalue ? [cfgvalue] : ['time', 'temp', 'date']);

			var CELL = 12, PAD = 10;
			var canvas = E('canvas', {
				width: 27 * CELL + PAD * 2,
				height: 5 * CELL + PAD * 2,
				style: 'background:#1a1a1a;border-radius:8px;display:block;margin:6px 0 14px 0'
			});
			previewCanvas = canvas;

			// 初始展示首个勾选项的点阵
			var initialText = '12:34:56';
			if (selectedValues.length > 0) {
				for (var k = 0; k < PAGE_ITEMS.length; k++) {
					if (PAGE_ITEMS[k].id === selectedValues[0]) {
						initialText = PAGE_ITEMS[k].preview;
						break;
					}
				}
			}
			drawLedFrame(canvas, textToColumns(initialText));

			// 组装带复选框的交互卡片
			var cardsContainer = E('div', {
				style: 'display:flex;flex-wrap:wrap;gap:10px;margin-top:6px'
			});

			PAGE_ITEMS.forEach(function(item) {
				var isChecked = selectedValues.indexOf(item.id) !== -1;

				var cb = E('input', {
					type: 'checkbox',
					'data-page-id': item.id,
					checked: isChecked,
					style: 'margin-right:8px;transform:scale(1.2);cursor:pointer'
				});

				var card = E('label', {
					style: 'display:inline-flex;align-items:center;padding:7px 14px;' +
					       'background:#f8f9fa;border:1px solid #ced4da;border-radius:6px;' +
					       'cursor:pointer;user-select:none;transition:all 0.15s ease;'
				}, [
					cb,
					E('span', {
						style: 'font-weight:500;font-size:13px;',
						click: function(ev) {
							// 点击文字只预览点阵，不额外触发两次 checkbox 事件
							updatePreview(item.preview);
						}
					}, item.icon + ' ' + item.label + ' (' + item.preview + ')')
				]);

				cb.addEventListener('change', function() {
					updatePreview(item.preview);
				});

				cardsContainer.appendChild(card);
			});

			return E('div', { 'class': 'cbi-value-field' }, [
				E('div', { style: 'margin-bottom:4px;font-size:12px;color:#6c757d' },
					_('Simulates the 27×5 LED matrix in real-time. Check the boxes to select pages for rotation; click any label to preview its matrix.')),
				canvas,
				cardsContainer
			]);
		};

		// 🌟 重写 formvalue：精准收集所有勾选的 checkbox 属性 data-page-id
		o.formvalue = function(section_id) {
			var checked = [];
			var cbs = document.querySelectorAll('input[type="checkbox"][data-page-id]');
			for (var i = 0; i < cbs.length; i++) {
				if (cbs[i].checked)
					checked.push(cbs[i].getAttribute('data-page-id'));
			}
			return checked.length > 0 ? checked : ['time'];
		};

		// 🌟 重写 write：将选中的数组原子写入 UCI 的 list pages 列表
		o.write = function(section_id, formvalue) {
			var values = Array.isArray(formvalue) ? formvalue : (formvalue ? [formvalue] : ['time']);
			return uci.set('athena-led-c', section_id, 'pages', values);
		};

		// ============================================================
		// 板块 2: 控制参数设置
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
		o.datatype = 'uinteger';
		o.default = '0';
		o.description = _('Seconds to stay on each page before switching. Set to 0 for manual button flipping only (no auto-rotation). Greater than 0 enables automatic flipping.');

		o = s.option(form.Value, 'net_interface', _('Traffic Monitor Interface'));
		o.default = 'br-lan';
		o.description = _('Network interface to monitor throughput for download and upload pages.');
		netDevices.forEach(function(dev) {
			var name = dev.getName();
			if (!/^(lo$|sit|gre|ifb|ip6|teql|erspan|miireg|phy)/.test(name))
				o.value(name);
		});

		// ============================================================
		// 板块 3: 高级调优
		// ============================================================
		s = m.section(form.NamedSection, 'main', 'main', _('Advanced Tuning'));
		s.addremove = false;

		o = s.option(form.Value, 'poll_ms', _('Sampling Interval (ms)'));
		o.datatype = 'range(100,1000)';
		o.default = '250';
		o.description = _('Key scanning and sensor refresh interval in milliseconds (default 250ms).');

		// ============================================================
		// 板块 4: 服务操作
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
				mapEl,
				buildButtonHelpBox()
			]);
		});
	}
});
