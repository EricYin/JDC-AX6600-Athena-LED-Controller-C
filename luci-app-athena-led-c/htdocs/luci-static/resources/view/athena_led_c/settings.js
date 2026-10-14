'use strict';
'require view';
'require uci';
'require form';
'require fs';
'require ui';
'require poll';
'require network';

var LUCI_APP_VERSION = '0.3.0';
var CORE_VERSION = '0.3.0';

/*
 * Athena LED Controller — LuCI JS Settings View
 * For JDCloud AX6600 (RE-CS-02) Pure C LED Daemon
 * Single-screen static display, direct checkbox selection with live preview
 */

function getServiceStatus() {
	return Promise.all([
		fs.read('/var/run/athena-led-c.pid').catch(function() { return ''; }),
		fs.read('/var/run/athena-led-c.version').catch(function() { return ''; }),
		fs.read('/var/run/athena-led-c.screen').catch(function() { return ''; }),
		uci.load('athena-led-c').then(function() {
			return uci.get('athena-led-c', 'main', 'screen_on');
		}).catch(function() { return ''; })
	]).then(function(res) {
		var pid = (res[0] || '').trim();
		var ver = (res[1] || '').trim() || CORE_VERSION;
		var screenState = (res[2] || '').trim();
		if (!screenState)
			screenState = (res[3] != null && res[3] !== '') ? String(res[3]).trim() : '1';
		var screenOn = (screenState !== '0');

		if (pid && /^\d+$/.test(pid)) {
			return fs.read('/proc/' + pid + '/cmdline').then(function(cmdline) {
				if ((cmdline || '').indexOf('athena-led-c') !== -1)
					return { running: true, pid: pid, version: ver, screen_on: screenOn };
				return checkPidof(ver, screenOn);
			}, function() {
				return checkPidof(ver, screenOn);
			});
		}
		return checkPidof(ver, screenOn);
	}).catch(function() {
		return checkPidof(CORE_VERSION, true);
	});
}

function checkPidof(ver, screenOn) {
	return fs.exec_direct('/bin/pidof', ['athena-led-c']).then(function(res) {
		var pid = (res || '').trim().split(/\s+/)[0];
		if (pid && /^\d+$/.test(pid))
			return { running: true, pid: pid, version: ver || CORE_VERSION, screen_on: screenOn };
		return { running: false, version: ver || CORE_VERSION, screen_on: screenOn };
	}).catch(function() {
		return { running: false, version: ver || CORE_VERSION, screen_on: screenOn };
	});
}

function callInitAction(action, silent) {
	return fs.exec('/etc/init.d/athena-led-c', [action]).then(function(res) {
		if (res.code !== 0)
			throw new Error(_('Command failed with code %d').format(res.code));
		if (!silent) ui.addNotification(null, E('p', _('Service action "%s" executed successfully.').format(action)), 'info');
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

// 8 个纯本地功能项（无日出日落）
var PAGE_ITEMS = [
	{ id: 'time',     label: _('Time'),        icon: '🕒', preview: '12^34^56' },
	{ id: 'net_down', label: _('Download'),    icon: '⬇️', preview: 'D^;12.5M' },
	{ id: 'net_up',   label: _('Upload'),      icon: '⬆️', preview: 'U^;;1.8M' },
	{ id: 'mem',      label: _('Memory'),      icon: '💾', preview: 'M^;35%' },
	{ id: 'cpu',      label: _('CPU'),         icon: '💻', preview: 'C^;12%' },
	{ id: 'temp',     label: _('Temperature'), icon: '🌡️', preview: 'T^45.52' },
	{ id: 'date',     label: _('Date'),        icon: '📅', preview: '10-14' },
	{ id: 'lunar',    label: _('Lunar'),       icon: '🏮', preview: 'L:9.4' },
	{ id: 'text',     label: _('Custom Text'), icon: '💬', preview: 'ATHENA' }
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
				E('strong', {}, _('Long Press (2s): ')),
				_('Toggle screen standby power (turn display ON or OFF).')
			])
		])
	]);
}

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(uci.load('athena-led-c'), {}),
			network.getDevices().catch(function() { return []; })
		]);
	},

	render: function(data) {
		var netDevices = data[1] || [];
		var m, s, o;
		var currentCustomText = 'ATHENA';

		m = new form.Map('athena-led-c');

		s = m.section(form.NamedSection, 'main', 'main', _('General Settings'));
		s.addremove = false;

		// ============================================================
		// 1. Preview and independent page selection
		// ============================================================
		// The preview is intentionally independent from checkbox changes:
		// selecting/deselecting a page never moves the preview.
		var previewPageId = null;

		var previewSection = E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, _('Screen Preview')),
			E('div', { 'class': 'cbi-section-descr' }, [
				E('p', { style: 'margin:4px 0' },
					_('Click a feature button to preview it.'))
			])
		]);

		var previewCanvas = E('canvas', {
			width: 27 * 12 + 10 * 2,
			height: 5 * 12 + 10 * 2,
			style: 'background:#1a1a1a;border-radius:8px;display:block;margin:8px 0 12px 0'
		});
		var previewButtons = E('div', {
			style: 'display:flex;flex-wrap:wrap;gap:8px;margin:6px 0 10px 0'
		});

		function updatePreview(pageId) {
			var item = PAGE_ITEMS.filter(function(candidate) { return candidate.id == pageId; })[0];
			previewPageId = item ? item.id : PAGE_ITEMS[0].id;

			var previewItem = PAGE_ITEMS.filter(function(candidate) { return candidate.id == previewPageId; })[0];
			var previewStr = (previewItem.id === 'text' && currentCustomText != null)
				? currentCustomText
				: previewItem.preview;
			drawLedFrame(previewCanvas, textToColumns(previewStr));

			for (var i = 0; i < previewButtons.children.length; i++) {
				var button = previewButtons.children[i];
				button.style.background = (button.getAttribute('data-page-id') == previewPageId)
					? '#ffb020' : '#f8f9fa';
				button.style.color = (button.getAttribute('data-page-id') == previewPageId)
					? '#231000' : '#202124';
				button.style.borderColor = (button.getAttribute('data-page-id') == previewPageId)
					? '#d99616' : '#ced4da';
			}
		}

		PAGE_ITEMS.forEach(function(item) {
			var button = E('button', {
				type: 'button',
				'data-page-id': item.id,
				'class': 'cbi-button cbi-button-neutral',
				style: 'padding:6px 12px;font-size:13px;font-weight:500;cursor:pointer;border:1px solid #ced4da;border-radius:6px;background:#f8f9fa;color:#202124'
			}, item.icon + ' ' + item.label);

			button.addEventListener('click', function(ev) {
				ev.preventDefault();
				updatePreview(item.id);
			});

			previewButtons.appendChild(button);
		});

		updatePreview(PAGE_ITEMS[0].id);

		previewSection.appendChild(previewButtons);
		previewSection.appendChild(previewCanvas);

		function findItem(pageId) {
			return PAGE_ITEMS.filter(function(item) {
				return item.id == pageId;
			})[0];
		}

		var pagesOption = s.option(form.Value, 'pages', _('Display Pages'));
		pagesOption.rmempty = false;
		pagesOption.forcewrite = true;
		pagesOption.description = _('Only checked features are saved to the rotation list. Use the arrows to change the display order.');

		pagesOption.load = function(section_id) {
			var value = this.map.data.get(this.map.config, section_id, this.option);
			var configured = (value == null) ? [] : (Array.isArray(value) ? value : String(value).split(/[\s,]+/));
			var selected = [];
			for (var i = 0; i < configured.length; i++) {
				var p = configured[i];
				if (findItem(p) && selected.indexOf(p) == -1)
					selected.push(p);
			}

			// A missing pages option means all pages are enabled (legacy behavior)
			if (value == null && configured.length == 0)
				selected = PAGE_ITEMS.map(function(item) { return item.id; });

			this.cfgvalue(section_id, selected);
			return selected;
		};

		pagesOption.renderWidget = function(section_id, option_index, cfgvalue) {
			// Keep a stable value object for this widget. LuCI re-renders the
			// map during save, so formvalue() must not depend on DOM lookups.
			var selectedValues = Array.isArray(cfgvalue) ? cfgvalue.slice() : [];

			var cardsContainer = E('div', {
				style: 'display:flex;flex-wrap:wrap;gap:10px;margin-top:6px;'
			});

			function renderCards() {
				cardsContainer.innerHTML = '';

				// Checked cards follow the configured display order. Unchecked
				// cards stay visible below them so all features remain editable.
				var displayItems = selectedValues.map(findItem).filter(Boolean);
				PAGE_ITEMS.forEach(function(item) {
					if (selectedValues.indexOf(item.id) == -1)
						displayItems.push(item);
				});

				displayItems.forEach(function(item) {
					var selectedIndex = selectedValues.indexOf(item.id);
					var row = E('div', {
						style: 'display:inline-flex;align-items:center;gap:8px;padding:7px 12px;' +
						       'background:#f8f9fa;border:1px solid #ced4da;border-radius:6px;user-select:none;'
					});

					if (selectedIndex != -1) {
						row.appendChild(E('span', {
							'class': 'cbi-badge',
							style: 'min-width:20px;height:20px;line-height:20px;text-align:center;' +
							       'background:#ffb020;color:#231000;border-radius:999px;font-size:11px;font-weight:600;'
						}, String(selectedIndex + 1)));
					}

					var cb = E('input', {
						type: 'checkbox',
						'data-page-id': item.id,
						style: 'margin:0;transform:scale(1.15);cursor:pointer;'
					});

					// Set the DOM property, not the HTML attribute. E() calls
					// setAttribute(), where checked="false" still means checked.
					cb.checked = selectedIndex != -1;

					var label = E('span', { style: 'font-weight:500;font-size:13px;cursor:pointer;' },
						item.icon + ' ' + item.label);

					function syncSelection() {
						var index = selectedValues.indexOf(item.id);
						if (cb.checked && index == -1)
							selectedValues.push(item.id);
						else if (!cb.checked && index != -1)
							selectedValues.splice(index, 1);

						pagesOption.cfgvalue(section_id, selectedValues);
						renderCards();
					}

					cb.addEventListener('click', function(ev) {
						ev.stopPropagation();
						syncSelection();
					});

					label.addEventListener('click', function(ev) {
						cb.checked = !cb.checked;
						ev.preventDefault();
						ev.stopPropagation();
						syncSelection();
					});

					row.appendChild(cb);
					row.appendChild(label);

					if (selectedIndex != -1) {
						var orderButtons = E('span', {
							style: 'display:inline-flex;gap:4px;margin-left:auto;'
						});

						var moveUp = E('button', {
							type: 'button',
							title: _('Move up'),
							'aria-label': _('Move up'),
							style: 'width:26px;height:26px;min-width:26px;min-height:26px;padding:0;line-height:24px;text-align:center;font-size:13px;cursor:pointer;box-sizing:border-box;',
							'class': 'cbi-button cbi-button-neutral'
						}, '↑');
						moveUp.disabled = selectedIndex == 0;

						var moveDown = E('button', {
							type: 'button',
							title: _('Move down'),
							'aria-label': _('Move down'),
							style: 'width:26px;height:26px;min-width:26px;min-height:26px;padding:0;line-height:24px;text-align:center;font-size:13px;cursor:pointer;box-sizing:border-box;',
							'class': 'cbi-button cbi-button-neutral'
						}, '↓');
						moveDown.disabled = selectedIndex == selectedValues.length - 1;

						function moveSelected(delta) {
							var target = selectedIndex + delta;
							if (target < 0 || target >= selectedValues.length)
								return;

							var tmp = selectedValues[selectedIndex];
							selectedValues[selectedIndex] = selectedValues[target];
							selectedValues[target] = tmp;

							pagesOption.cfgvalue(section_id, selectedValues);
							renderCards();
						}

						moveUp.addEventListener('click', function(ev) {
							ev.preventDefault();
							ev.stopPropagation();
							moveSelected(-1);
						});

						moveDown.addEventListener('click', function(ev) {
							ev.preventDefault();
							ev.stopPropagation();
							moveSelected(1);
						});

						orderButtons.appendChild(moveUp);
						orderButtons.appendChild(moveDown);
						row.appendChild(orderButtons);
					}

					cardsContainer.appendChild(row);
				});
			}

			renderCards();
			return E('div', {}, [ cardsContainer ]);
		};

		pagesOption.formvalue = function(section_id) {
			var value = this.cfgvalue(section_id);
			var checked = (Array.isArray(value) ? value : []).filter(function(pageId, index) {
				return findItem(pageId) && value.indexOf(pageId) == index;
			});

			// The C daemon requires at least one page.
			return checked.length > 0 ? checked : ['time'];
		};

		pagesOption.write = function(section_id, formvalue) {
			var values = Array.isArray(formvalue) ? formvalue : (formvalue ? [formvalue] : ['time']);
			values = values.filter(function(pageId, index) {
				return findItem(pageId) && values.indexOf(pageId) == index;
			});

			if (values.length == 0)
				values = ['time'];

			this.cfgvalue(section_id, values);
			return this.map.data.set(this.map.config, section_id, this.option, values);
		};

		// Initialize the preview from the first enabled page, but do not bind
		// preview navigation to checkbox state changes.
		pagesOption.onchange = function(ev, section_id, value) {
			if (Array.isArray(value) && value.length > 0)
				updatePreview(value[0]);
		};

		// ============================================================
		// 2. 基础控制选项
		// ============================================================
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
		o = s.option(form.Value, 'custom_text', _('Custom Text Content'));
		o.default = 'ATHENA';
		o.rmempty = false;
		o.forcewrite = true;
		o.description = _('Text displayed on the custom text page. Automatically restricted to 27 LED dot matrix columns.');

		o.load = function(section_id) {
			var val = this.map.data.get(this.map.config, section_id, this.option);
			if (val == null || val === '')
				val = 'ATHENA';
			currentCustomText = String(val);
			this.cfgvalue(section_id, currentCustomText);
			return currentCustomText;
		};

		o.validate = function(section_id, value) {
			var val = (value != null && value !== '') ? String(value).trim() : currentCustomText;
			if (!val || val.length === 0)
				return _('Custom text cannot be empty.');
			var cols = textToColumns(val);
			if (cols.length > 27)
				return _('Text exceeds 27 LED columns (currently %d columns).').format(cols.length);
			return true;
		};

		o.formvalue = function(section_id) {
			var node = document.getElementById('widget.cbid.athena-led-c.' + section_id + '.custom_text') ||
			           document.querySelector('input[name="cbid.athena-led-c.' + section_id + '.custom_text"]');
			var val = node ? node.value : (currentCustomText || this.cfgvalue(section_id) || 'ATHENA');
			val = String(val || '').trim();
			while (val.length > 0 && textToColumns(val).length > 27)
				val = val.slice(0, -1);
			return (val.length > 0) ? val : 'ATHENA';
		};

		o.write = function(section_id, formvalue) {
			var val = (formvalue != null && formvalue !== '') ? String(formvalue).trim() : 'ATHENA';
			while (val.length > 0 && textToColumns(val).length > 27)
				val = val.slice(0, -1);
			if (!val) val = 'ATHENA';
			currentCustomText = val;
			this.cfgvalue(section_id, val);
			return this.map.data.set(this.map.config, section_id, this.option, val);
		};

		o.renderWidget = function(section_id, option_index, cfgvalue) {
			var initialVal = (cfgvalue != null && cfgvalue !== '') ? String(cfgvalue) : (currentCustomText || 'ATHENA');
			currentCustomText = initialVal;

			var inputEl = E('input', {
				type: 'text',
				id: 'widget.cbid.athena-led-c.' + section_id + '.custom_text',
				name: 'cbid.athena-led-c.' + section_id + '.custom_text',
				'class': 'cbi-input-text',
				value: initialVal,
				style: 'max-width:260px;font-family:monospace;font-weight:bold;'
			});

			var widthHint = E('span', {
				style: 'margin-left:8px;font-size:12px;color:#6c757d;'
			});

			function updateInputState() {
				var val = inputEl.value;
				while (val.length > 0 && textToColumns(val).length > 27) {
					val = val.slice(0, -1);
					inputEl.value = val;
				}
				var colCount = textToColumns(inputEl.value).length;
				widthHint.textContent = _('Dot Matrix Width: %d/27 columns').format(colCount);
				currentCustomText = inputEl.value;
				o.cfgvalue(section_id, currentCustomText);
				if (previewPageId === 'text')
					updatePreview('text');
			}

			inputEl.addEventListener('input', updateInputState);
			inputEl.addEventListener('keyup', updateInputState);
			inputEl.addEventListener('change', updateInputState);

			updateInputState();

			return E('div', { style: 'display:flex;align-items:center;flex-wrap:wrap;gap:8px;' }, [
				inputEl,
				widthHint
			]);
		};


		o = s.option(form.Value, 'net_interface', _('Traffic Monitor Interface'));
		o.default = 'wan';
		o.description = _('Network interface to monitor throughput for download and upload pages.');
		netDevices.forEach(function(dev) {
			var name = dev.getName();
			if (!/^(lo$|sit|gre|ifb|ip6|teql|erspan|miireg|phy)/.test(name))
				o.value(name);
		});

		o = s.option(form.Value, 'poll_ms', _('Sampling Interval (ms)'));
		o.datatype = 'range(100,1000)';
		o.default = '250';
		o.description = _('Sampling and display refresh interval in milliseconds (default 250ms).');

		o = s.option(form.Value, 'indicator_cpu_percent', _('CPU Indicator Threshold (%)'));
		o.datatype = 'ufloat';
		o.default = '5';
		o.description = _('Light the CPU indicator when total CPU usage exceeds this percentage.');

		o = s.option(form.Value, 'indicator_net_kbytes', _('Network Indicator Threshold (KB/s)'));
		o.datatype = 'ufloat';
		o.default = '10';
		o.description = _('Light the up/down indicators when the corresponding direction exceeds this rate in KB/s (1 KB = 1000 bytes).');

		o = s.option(form.Value, 'net_smoothing_secs', _('Network Rate Smoothing (seconds)'));
		o.datatype = 'and(ufloat,min(0),max(60))';
		o.default = '2';
		o.description = _('Exponential moving average time constant for network rates. Set to 0 to disable smoothing; larger values change more slowly.');

		// ============================================================
		// 3. 服务操作按钮
		// ============================================================
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
		// 4. 渲染视图与状态轮询
		// ============================================================
		return m.render().then(function(mapEl) {
			var statusText = E('em', {}, _('Collecting data...'));
			var screenControlRow = E('div', {
				style: 'margin-top:10px;padding-top:8px;border-top:1px solid #e9ecef;display:flex;align-items:center;flex-wrap:wrap;gap:12px;'
			});

			var statusBox = E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, _('Running Status')),
				E('div', { 'class': 'cbi-section-descr' }, [
					statusText,
					screenControlRow
				])
			]);

			function updateStatus() {
				return getServiceStatus().then(function(st) {
					var verInfo = ' | ' + _('Daemon') + ': v' + (st.version || CORE_VERSION) +
					              ' | ' + _('WebUI') + ': v' + LUCI_APP_VERSION;
					statusText.innerHTML = '';
					if (st.running) {
						statusText.appendChild(E('span', { 'style': 'color:green;font-weight:bold' }, _('RUNNING')));
						statusText.appendChild(document.createTextNode(' | PID: ' + st.pid + verInfo));
					} else {
						statusText.innerHTML = '<span style="color:red;font-weight:bold">' + _('NOT RUNNING') + '</span>' + verInfo;
					}

					screenControlRow.innerHTML = '';

					var screenStateBadge = st.screen_on
						? E('span', { 'class': 'cbi-badge', style: 'background:#28a745;color:#fff;font-weight:600;padding:2px 8px;border-radius:4px;' }, _('Screen On'))
						: E('span', { 'class': 'cbi-badge', style: 'background:#6c757d;color:#fff;font-weight:600;padding:2px 8px;border-radius:4px;' }, _('Screen Standby'));

					var screenStatusLabel = E('span', { style: 'display:inline-flex;align-items:center;gap:6px;' }, [
						E('strong', {}, _('Screen Status: ')),
						screenStateBadge
					]);

					var toggleBtn = E('button', {
						type: 'button',
						'class': st.screen_on ? 'cbi-button cbi-button-neutral' : 'cbi-button cbi-button-action',
						style: 'padding:4px 12px;font-size:12px;font-weight:600;cursor:pointer;'
					}, st.screen_on ? _('Turn Off Screen') : _('Turn On Screen'));

					var persistNote = E('span', {
						style: 'color:#6c757d;font-size:12px;'
					}, _('(Instant & persistent across reboots)'));

					toggleBtn.addEventListener('click', function(ev) {
						ev.preventDefault();
						toggleBtn.disabled = true;
						return callInitAction('screen_toggle', true).then(function() {
							return updateStatus();
						}).finally(function() {
							toggleBtn.disabled = false;
						});
					});

					screenControlRow.appendChild(screenStatusLabel);
					screenControlRow.appendChild(toggleBtn);
					screenControlRow.appendChild(persistNote);
				});
			}

			updateStatus();
			poll.add(updateStatus, 3);

			return E('div', {}, [
				E('h2', { name: 'content' }, _('Athena LED Controller')),
				statusBox,
				previewSection,
				mapEl,
				buildButtonHelpBox()
			]);
		});
	}
});
