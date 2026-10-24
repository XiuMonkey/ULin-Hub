/* ══════════════════════════════════════════════════════════════
   浮光掠影 · 戈壁星野文学社
   版式与 haiyan.beilu.cyou 的题壁同构：竖排纸片 + 页签视图 + 一句一轮的玻璃小条
   ══════════════════════════════════════════════════════════════ */
(() => {
  'use strict';

  const K_MINE  = 'fuguang.m.v2';
  const K_LIKE  = 'fuguang.like.v2';
  const K_DRAFT = 'fuguang.draft.v2';
  const K_LAST  = 'fuguang.last.v2';

  const MOODS = ['闪光', '微小', '滑稽', '糟糕', '感人', '绝望', '混乱', '快乐'];
  const MAXLEN = 60;
  const COOLDOWN = 20000;

  /* ── 最初上墙的那些片刻 ── */
  const SEED = [
    { mood:'闪光', text:'下晚自习冲出来，整条路的路灯刚好一盏接一盏亮起来，像有人在替我开道。', who:'阿栗', when:'23:47', likes:23 },
    { mood:'快乐', text:'半夜跑下楼，整条银河压得很低。我站着看了很久，冻得发抖也没舍得回去。', who:'禾', when:'00:38', likes:73 },
    { mood:'微小', text:'食堂阿姨多给我盛了一勺土豆，说“看你太瘦”。我在窗口前站了三秒才说出谢谢。', who:'林', when:'12:04', likes:41 },
    { mood:'混乱', text:'凌晨两点，论文只写了标题。我把椅子转到窗边看了一会儿月亮，觉得它也没写完。', who:'无名', when:'02:11', likes:88 },
    { mood:'绝望', text:'给妈妈打电话，她说“没事就挂了吧”。挂了之后我才想起来，我其实是有事的。', who:'小满', when:'10-19', likes:57 },
    { mood:'快乐', text:'链条掉了，蹲在路边弄了十分钟，手上全是油。站起来时风正好吹过来，特别爽。', who:'老周', when:'10-18', likes:19 },
    { mood:'感人', text:'他把我的伞带到宿舍楼下，说“猜你没带”。那时雨已经停了两小时。', who:'十九', when:'10-17', likes:64 },
    { mood:'滑稽', text:'在电梯里对陌生人笑了一下，他惊慌地看了看自己身后。', who:'铁皮', when:'10-16', likes:102 },
    { mood:'糟糕', text:'面试官问我五年规划，我说想开一家不赚钱的书店。他沉默很久，说“挺好的”。', who:'阿树', when:'10-15', likes:36 },
    { mood:'闪光', text:'白杨叶翻过来的时候是银的，一整排一起翻过去，像有人在很远的地方朝我挥手。', who:'江', when:'10-14', likes:47 },
  ].map((m, i) => ({ ...m, id: 'seed-' + i, mine: false }));

  const $  = s => document.querySelector(s);
  const $$ = s => [...document.querySelectorAll(s)];

  const wall      = $('#wall');
  const wallMine  = $('#wallMine');
  const moodFilter= $('#moodFilter');
  const moodPick  = $('#moodPick');
  const input     = $('#content');
  const nick      = $('#nick');
  const cN        = $('#cN');
  const send      = $('#send');
  const msg       = $('#msg');
  const scrollEl  = $('#scroll');
  const ticker    = $('#ticker');
  const miLine    = $('#miLine');
  const guideWrap = $('#guideWrap');

  /* ── 存取 ── */
  const load = (k, fb) => { try { const v = localStorage.getItem(k); return v ? JSON.parse(v) : fb; } catch { return fb; } };
  const save = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch {} };

  let mine  = load(K_MINE, []);
  let liked = new Set(load(K_LIKE, []));
  let filter = 'all';
  let picked = '';

  const all = () => [...mine, ...SEED];

  /* ── 纸片 ── */
  /* 竖排纸片的宽度不要自己按字号推算：竖排里「列内推进」和「列距」是两个方向，
     推算差一两个字就整列被 overflow 裁掉，而且看不出来（只是话少了一截）。
     做法是先给足宽度让浏览器自然分列，再量 .slip-body 的真实宽度收回来。 */
  const SLIP_MAX = 246;
  const SLIP_MIN = 110;   // 再窄落款就放不下「心情 + 署名 + 星」三样
  const SLIP_PAD = 24;

  function fitSlip(el) {
    const b = el.querySelector('.slip-body');
    if (!b) return;
    const w = Math.ceil(b.getBoundingClientRect().width);
    if (!w) return;   // 视图还 hidden，量不出东西；等切换过去再量
    el.style.width = Math.min(Math.max(w + SLIP_PAD, SLIP_MIN), SLIP_MAX) + 'px';
  }

  function fitWall(node) {
    if (!node) return;
    [...node.children].forEach(fitSlip);
  }

  function slipEl(m, i) {
    const el = document.createElement('div');
    el.className = 'slip';
    el.style.animationDelay = Math.min(i * 45, 400) + 'ms';
    el.style.width = SLIP_MAX + 'px';   // 先放开，等挂到 DOM 上再量

    const body = document.createElement('p');
    body.className = 'slip-body';
    body.textContent = m.text;

    const foot = document.createElement('div');
    foot.className = 'slip-foot';

    const mo = document.createElement('span');
    mo.className = 'slip-mood';
    mo.textContent = m.mood;

    const by = document.createElement('span');
    by.className = 'slip-by';
    // 窄纸片里塞不下「署名 · 时间」两样，署名优先，时间放到悬停里
    by.textContent = m.who || '匿名';
    el.title = (m.who || '匿名') + ' · ' + (m.when || '');

    foot.append(mo, by);

    if (m.mine) {
      const del = document.createElement('button');
      del.type = 'button';
      del.className = 's-del';
      del.textContent = '撤';
      del.title = '撤回这一则';
      del.addEventListener('click', () => {
        mine = mine.filter(x => x.id !== m.id);
        save(K_MINE, mine);
        render();
        say('已撤回。就当它从没发生过。');
      });
      foot.appendChild(del);
    } else {
      // 自己的那则不放星数：纸片只有九十来像素宽，撤 + 署名 + 星三样摆不下，
      // 「撤回」比「给自己点星」要紧。
      const base = m.likes || 0;
      const fl = document.createElement('button');
      fl.type = 'button';
      fl.className = 's-flower' + (liked.has(m.id) ? ' on' : '');
      fl.textContent = String(base + (liked.has(m.id) ? 1 : 0));
      fl.setAttribute('aria-label', '点亮一颗星');
      fl.addEventListener('click', () => {
        if (liked.has(m.id)) liked.delete(m.id); else liked.add(m.id);
        save(K_LIKE, [...liked]);
        const on = liked.has(m.id);
        fl.classList.toggle('on', on);
        fl.textContent = String(base + (on ? 1 : 0));
        if (on) {
          fl.classList.add('just');
          setTimeout(() => fl.classList.remove('just'), 520);
        }
      });
      foot.appendChild(fl);
    }

    el.append(body, foot);
    return el;
  }

  function fillWall(node, list, emptyText) {
    node.innerHTML = '';
    if (!list.length) {
      const b = document.createElement('div');
      b.className = 'slip blank';
      b.textContent = emptyText;
      node.appendChild(b);
      return;
    }
    const frag = document.createDocumentFragment();
    list.forEach((m, i) => frag.appendChild(slipEl(m, i)));
    node.appendChild(frag);
    // 挂上 DOM 之后才有真实布局，这时候逐张收窄（视图隐藏时内部会跳过）
    fitWall(node);
  }

  function render() {
    const list = all().filter(m => filter === 'all' || m.mood === filter);
    fillWall(wall, list, '墙上还空着——第一句就是你。');
    fillWall(wallMine, mine, '还没有。第一句就是你。');

    const total = all().length;
    const today = mine.filter(m => {
      const d = new Date(m.at || 0);
      const n = new Date();
      return d.toDateString() === n.toDateString();
    }).length;

    $('#liveN').textContent = total;
    $('#totalN').textContent = total;
    $('#todayN').textContent = today;
    $('#mineN').textContent = mine.length + ' 则';
    $('#wallN').textContent = filter === 'all' ? '左右滑动' : MOODS.includes(filter) ? filter + ' · ' + list.length + ' 则' : '左右滑动';
  }

  /* ── 心情 ── */
  function buildMoods(node, items, activeKey, onPick) {
    node.innerHTML = '';
    items.forEach(k => {
      const b = document.createElement('button');
      b.type = 'button';
      b.textContent = k === 'all' ? '全部' : k;
      b.dataset.mood = k;
      if (k === activeKey) b.classList.add('on');
      b.addEventListener('click', () => onPick(k, b, node));
      node.appendChild(b);
    });
  }

  /* ── 一句 ── */
  let tickTimer, tickIdx = 0;
  function tickPaint() {
    const pool = all().map(m => m.text).filter(Boolean);
    if (!pool.length) return;
    tickIdx = ((tickIdx % pool.length) + pool.length) % pool.length;
    const t = pool[tickIdx];
    miLine.textContent = t.length > 26 ? t.slice(0, 26) + '……' : t;
  }
  function tickNext() {
    const pool = all().filter(m => m.text);
    if (!pool.length) return;
    ticker.classList.add('fade');
    setTimeout(() => {
      tickIdx = (tickIdx + 1) % pool.length;
      tickPaint();
      ticker.classList.remove('fade');
    }, 320);
  }
  function tickStart() {
    tickPaint();
    clearInterval(tickTimer);
    tickTimer = setInterval(tickNext, 5200);
  }

  /* ── 页签 ── */
  function go(view) {
    $$('.view').forEach(v => v.classList.toggle('on', v.dataset.view === view));
    $$('.nav .tab').forEach(t => t.classList.toggle('on', t.dataset.go === view));
    scrollEl.scrollTop = 0;
    // 切过去之后墙才可见，这时才量得准；隐藏状态下量出来是 0，会把纸片压成最窄
    if (view === 'wall') fitWall(wall);
    if (view === 'write') fitWall(wallMine);
    try { history.replaceState(null, '', '#' + view); } catch {}
  }

  /* ── 提示 ── */
  function say(t) { msg.textContent = t; }

  /* ── 上墙 ── */
  function syncCount() {
    cN.textContent = input.value.length + ' / ' + MAXLEN;
    cN.classList.toggle('full', input.value.length >= MAXLEN);
  }

  function submit() {
    const text = input.value.trim();
    if (!text) { say('还没写呢。一句也行。'); input.focus(); return; }

    const last = Number(load(K_LAST, 0)) || 0;
    const left = COOLDOWN - (Date.now() - last);
    if (left > 0) { say('同一人二十秒一条，还有 ' + Math.ceil(left / 1000) + ' 秒。'); return; }

    mine.unshift({
      id: 'mine-' + Date.now().toString(36),
      mood: picked || '微小',
      text,
      who: (nick.value || '').trim() || '匿名',
      when: new Date().toTimeString().slice(0, 5),
      at: Date.now(),
      likes: 0,
      mine: true,
    });
    save(K_MINE, mine);
    save(K_LAST, Date.now());
    try { localStorage.removeItem(K_DRAFT); } catch {}

    input.value = '';
    picked = '';
    [...moodPick.children].forEach(c => c.classList.remove('on'));
    syncCount();

    // 新的一句要能立刻看见：先回到「全部」，再翻到片刻
    filter = 'all';
    [...moodFilter.children].forEach(c => c.classList.toggle('on', c.dataset.mood === 'all'));

    render();
    go('wall');
    say('已上墙。这一帧留下来了。');
    tickStart();
  }

  /* ── 绑定 ── */
  buildMoods(moodFilter, ['all', ...MOODS], 'all', (k, b, node) => {
    [...node.children].forEach(c => c.classList.toggle('on', c === b));
    filter = k;
    render();
  });

  buildMoods(moodPick, MOODS, '', (k, b, node) => {
    const wasOn = b.classList.contains('on');
    [...node.children].forEach(c => c.classList.remove('on'));
    if (!wasOn) { b.classList.add('on'); picked = k; } else { picked = ''; }
  });

  $$('.nav .tab').forEach(t => t.addEventListener('click', () => go(t.dataset.go)));

  send.addEventListener('click', submit);
  input.addEventListener('input', () => { syncCount(); save(K_DRAFT, input.value); });
  input.addEventListener('keydown', e => {
    if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); submit(); }
  });

  const draft = load(K_DRAFT, '');
  if (typeof draft === 'string' && draft) input.value = draft;
  syncCount();

  ticker.addEventListener('click', tickNext);
  ticker.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); tickNext(); } });

  $('#guideBtn').addEventListener('click', () => { guideWrap.hidden = false; });
  $$('[data-close]').forEach(el => el.addEventListener('click', () => { guideWrap.hidden = true; }));
  document.addEventListener('keydown', e => { if (e.key === 'Escape') guideWrap.hidden = true; });

  render();
  tickStart();

  // 深链：#write / #about / #wall
  const hash = (location.hash || '').replace('#', '');
  if (['wall', 'write', 'about'].includes(hash)) go(hash);
})();
