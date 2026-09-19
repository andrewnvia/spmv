import re, sys
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.chart import BarChart, LineChart, Reference, Series
from openpyxl.chart.label import DataLabelList
from openpyxl.utils import get_column_letter as CL

SRC = sys.argv[1] if len(sys.argv) > 1 else 'summary.txt'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'spmv_results.xlsx'

# =============================================================== parse summary
txt = open(SRC).read()
cores = int(re.search(r'cores available at run time:\s*(\d+)', txt).group(1))
def sect(a, b): return txt[txt.index(a): txt.index(b)]
sP, s1, s2, s3, s4 = (sect(' PREDICT STEP', ' [1]'), sect(' [1]', ' [2]'), sect(' [2]', ' [3]'),
                      sect(' [3]', ' [4]'), sect(' [4]', ' [5]'))
MATS = ['ecology2', 'atmosmodd', 'lp_wood1p', 'heart1']

pred = {}
for line in sP.splitlines():
    f = line.split()
    if f and f[0] in MATS:
        pred[f[0]] = dict(m=int(f[1]), n=int(f[2]), nnz=int(f[3]), K=int(f[5]),
                          rowImb=float(f[11]), nnzImb=float(f[12]))
win = {}
for line in s1.splitlines():
    if '|' in line and line.split()[0] in MATS:
        p = [x.strip() for x in line.split('|')]
        a, b, c = p[0].split(), p[1].split(), p[2].split()
        win[a[0]] = dict(cseq=float(a[1]), eseq=float(a[2]), comp=float(b[0]), ccfg=b[1],
                         eomp=float(c[0]), ecfg=c[1])
thr = {}
for line in s2.splitlines():
    if '|' in line and line.split()[0] in MATS:
        parts = line.split('|')
        thr[parts[0].strip()] = [float(x) for x in ' '.join(parts[1:]).split()]
lb, cur = {}, None
for line in s3.splitlines():
    m = re.match(r'\s*-- (\S+) / (\S+)', line)
    if m: cur = (m.group(1), m.group(2)); lb[cur] = []; continue
    m = re.match(r'\s+(row|nnz) / (.+?)\s{2,}([\d.]+) \(', line)
    if m and cur:
        lab = 'nnz/any' if m.group(1) == 'nnz' else 'row/' + m.group(2)
        lb[cur].append((lab, float(m.group(3))))
sc, cur, maxdiff = {}, None, 0.0
for line in s4.splitlines():
    m = re.match(r'\s*-- (\S+) / (\S+)', line)
    if m: cur = (m.group(1), m.group(2)); sc[cur] = []; continue
    m = re.match(r'\s+(\S+)\s+(row|nnz)\s+(.*)', line)
    if m and cur:
        cells = re.findall(r'([\d.]+)/(\d+)%', m.group(3))
        if len(cells) == 5:
            S = [float(c[0]) for c in cells]; E = [int(c[1]) for c in cells]
            for s_, e_, t_ in zip(S, E, [1, 2, 4, 8, 16]):
                maxdiff = max(maxdiff, abs(100 * s_ / t_ - e_))
            lab = 'nnz/any' if m.group(2) == 'nnz' else 'row/' + m.group(1)
            sc[cur].append((lab, S))
assert len(pred) == 4 and len(win) == 4 and len(thr) == 4, (len(pred), len(win), len(thr))
assert len(lb) == 8 and all(len(v) == 6 for v in lb.values())
assert len(sc) == 8 and all(len(v) == 6 for v in sc.values())
print(f'parsed OK; max |E_given - S/t| = {maxdiff:.2f} pct-points (rounding only)')

# ================================================================== styles
FN = 'Arial'
def fnt(**k): return Font(name=FN, size=k.pop('size', 10), **k)
BLUE, BLK, BOLD = fnt(color='0000FF'), fnt(), fnt(bold=True)
HDRF, TITLE, NOTE = fnt(bold=True, color='FFFFFF'), fnt(bold=True, size=14), fnt(italic=True, color='595959')
SEC = fnt(bold=True, size=11, color='1F3864')
HFILL = PatternFill('solid', start_color='1F3864')
YFILL = PatternFill('solid', start_color='FFFF00')
GFILL = PatternFill('solid', start_color='F2F2F2')
thin = Side(style='thin', color='BFBFBF'); BORDER = Border(left=thin, right=thin, top=thin, bottom=thin)
CEN = Alignment(horizontal='center', vertical='center', wrap_text=True)

wb = Workbook()

def put(ws, ref, v, font=BLK, fmt=None, fill=None, align=None, border=True):
    c = ws[ref]; c.value = v; c.font = font
    if fmt: c.number_format = fmt
    if fill: c.fill = fill
    if align: c.alignment = align
    if border: c.border = BORDER
    return c

def header(ws, row, labels, col0=1):
    for i, lab in enumerate(labels):
        put(ws, f'{CL(col0 + i)}{row}', lab, HDRF, fill=HFILL, align=CEN)
    ws.row_dimensions[row].height = 32

def title(ws, text, sub=None):
    ws['A1'].value = text; ws['A1'].font = TITLE
    if sub: ws['A2'].value = sub; ws['A2'].font = NOTE

def widths(ws, first, rest, n=20):
    ws.column_dimensions['A'].width = first
    for i in range(2, n + 1): ws.column_dimensions[CL(i)].width = rest

def col_at_cm(ws, cm):
    x = 0.0
    for i in range(1, 60):
        w = ws.column_dimensions[CL(i)].width or 8.43
        x += (w * 7 + 5) / 37.8
        if x >= cm: return CL(i + 1)
    return 'Z'

def finish(ch, ttl, xt, yt, w=16, h=8.5, log=False, ymin=None):
    ch.title = ttl; ch.x_axis.title = xt; ch.y_axis.title = yt
    ch.x_axis.delete = False; ch.y_axis.delete = False
    ch.width, ch.height = w, h
    ch.legend.position = 'b'
    if log: ch.y_axis.scaling.logBase = 10
    if ymin is not None: ch.y_axis.scaling.min = ymin
    return ch

def bar(ws, cats, series, ttl, xt, yt, labels=False, **kw):
    ch = BarChart(); ch.type = 'col'; ch.grouping = 'clustered'
    for t, ref in series: ch.series.append(Series(ref, title=t))
    ch.set_categories(cats)
    if labels:
        ch.dataLabels = DataLabelList(); ch.dataLabels.showVal = True
        for a in ('showCatName', 'showSerName', 'showLegendKey', 'showPercent'): setattr(ch.dataLabels, a, False)
    return finish(ch, ttl, xt, yt, **kw)

def line(ws, cats, series, ttl, xt, yt, ideal=None, **kw):
    """series: list of (title, ref); ideal: ref drawn as dashed grey."""
    ch = LineChart()
    for t, ref in series:
        s = Series(ref, title=t); s.marker.symbol = 'circle'; s.marker.size = 6; s.smooth = False
        ch.series.append(s)
    if ideal is not None:
        s = Series(ideal, title='Ideal'); s.marker.symbol = 'none'; s.smooth = False
        s.graphicalProperties.line.dashStyle = 'dash'; s.graphicalProperties.line.solidFill = '808080'
        ch.series.append(s)
    ch.set_categories(cats)
    return finish(ch, ttl, xt, yt, ymin=0, **kw)

def ref(ws, c1, r1, c2=None, r2=None):
    return Reference(ws, min_col=c1, min_row=r1, max_col=c2 or c1, max_row=r2 or r1)

# =================================================================== README
ws = wb.active; ws.title = 'README'
widths(ws, 30, 100, 2)
title(ws, 'Program 1 -- OpenMP SpMV (CSR vs ELL): results workbook',
      f'Data source: summary.txt from run_all.sh   (node had {cores} cores; thread counts 1-16 were NOT oversubscribed)')
rows = [
 ('Sheet', 'Report item / contents'),
 ('2b_Memory', '2b: storage footprint CSR vs ELL, K/avg, padding %, predicted winner.  Charts: footprint (MB, log axis), K/avg vs ELL/CSR, padding %.'),
 ('2c_LoadBalance', '2c: modelled imbalance (max/avg per thread), 8-thread time for every schedule x partition, speedup vs static/row.  Charts: imbalance, CSR speedup, ELL speedup.'),
 ('2d_Winner', '2d + 2e(d): sequential vs best 8-thread config, winner per matrix, prediction check.  Charts: time (log), speedup vs sequential.'),
 ('2d_ScalingData', '2d: S(t) for every config, E(t)=S/t computed by formula.'),
 ('2d_ScalingCharts', '2d: speedup & efficiency per matrix (CSR vs ELL), and schedule sweeps for CSR and ELL (16 charts).'),
 ('2e_Bandwidth', '2e(b): GFLOP/s, GB/s, ELL useful bandwidth, achieved-vs-peak, roofline.  ENTER YOUR STREAM PEAK in the yellow cell.'),
 ('', ''),
 ('Colour legend', 'Blue text = value copied from your run output (edit these to update everything).  Black = formula.  Yellow fill = you must fill in / tune.'),
 ('', ''),
 ('Assumptions / notes', ''),
 ('1', 'All timings are the best-config or per-config averages printed by the programs (ms per SpMV). Scaling values S(t)=T1/Tt come from summary [4]; E(t)=S/t is recomputed here (matches printed E within rounding).'),
 ('2', 'partition=nnz is schedule-independent, so it appears once per kernel as "nnz/any".'),
 ('3', 'Imbalance columns (rowImb, nnzImb) are a MODEL computed from the .mtx row lengths (T=8), not measured thread times.'),
 ('4', 'Program GB/s counts 12 B per stored entry (4 B val + 4 B col idx + 4 B x load) INCLUDING ELL padding slots. Check: heart1 ELL 12*3,983,840 B / 0.4566 ms = 104.7 GB/s (matches). "ELL useful GB/s" = GB/s x (1 - padding).'),
 ('5', 'Peak bandwidth is not measured by run_all.sh. Run STREAM triad on the same node and type the number into 2e_Bandwidth!B4; the %-of-peak, roofline and the "Peak" bar then populate.'),
 ('6', 'OI = 0.17 flop/byte is the value given in the assignment.'),
 ('7', 'Predicted winner rule: ELL if K/avg <= threshold (2b_Memory!B2, default 1.5) else CSR. The 1.5 threshold is a heuristic, adjust it and justify it in the report.'),
]
for i, (a, b) in enumerate(rows, start=4):
    put(ws, f'A{i}', a, BOLD if i == 4 or a in ('Colour legend', 'Assumptions / notes') else BLK, border=False)
    put(ws, f'B{i}', b, BOLD if i == 4 else BLK, align=Alignment(wrap_text=True, vertical='top'), border=False)
    if a == 'Colour legend': ws[f'B{i}'].font = BLK

# ================================================================= 2b Memory
ws = wb.create_sheet('2b_Memory'); widths(ws, 18, 13, 16)
title(ws, '2b -- Format memory consumption: CSR vs ELL',
      'CSR bytes = 4*nnz + 4*nnz + 4*(m+1);  ELL bytes = 4*m*K + 4*m*K;  K = max nnz per row;  padding = (m*K - nnz)/(m*K)')
put(ws, 'A3', 'K/avg threshold', BOLD, border=False)
put(ws, 'B3', 1.5, BLUE, '0.00', YFILL)
put(ws, 'C3', '<- predict ELL if K/avg <= this value (heuristic, edit + justify)', NOTE, border=False)
header(ws, 4, ['matrix', 'm (rows)', 'n (cols)', 'nnz', 'avg nnz/row', 'K = max nnz/row', 'K / avg',
               'CSR bytes', 'ELL bytes', 'ELL / CSR', 'ELL padding %', 'CSR MB', 'ELL MB', 'predicted winner'])
for i, mat in enumerate(MATS):
    r = 5 + i; d = pred[mat]
    put(ws, f'A{r}', mat, BOLD)
    put(ws, f'B{r}', d['m'], BLUE, '#,##0'); put(ws, f'C{r}', d['n'], BLUE, '#,##0')
    put(ws, f'D{r}', d['nnz'], BLUE, '#,##0'); put(ws, f'F{r}', d['K'], BLUE, '#,##0')
    put(ws, f'E{r}', f'=D{r}/B{r}', fmt='0.00'); put(ws, f'G{r}', f'=F{r}/E{r}', fmt='0.00')
    put(ws, f'H{r}', f'=8*D{r}+4*(B{r}+1)', fmt='#,##0'); put(ws, f'I{r}', f'=8*B{r}*F{r}', fmt='#,##0')
    put(ws, f'J{r}', f'=I{r}/H{r}', fmt='0.00'); put(ws, f'K{r}', f'=(B{r}*F{r}-D{r})/(B{r}*F{r})', fmt='0.0%')
    put(ws, f'L{r}', f'=H{r}/1000000', fmt='0.00'); put(ws, f'M{r}', f'=I{r}/1000000', fmt='0.00')
    put(ws, f'N{r}', f'=IF(G{r}<=$B$3,"ELL","CSR")', BOLD, align=CEN)
cats = ref(ws, 1, 5, 1, 8)
ch = bar(ws, cats, [('CSR (MB)', ref(ws, 12, 5, 12, 8)), ('ELL (MB)', ref(ws, 13, 5, 13, 8))],
         'Storage footprint: CSR vs ELL', 'matrix', 'MB (log scale)', log=True)
ws.add_chart(ch, 'A11')
ch = bar(ws, cats, [('max / avg nnz per row (K/avg)', ref(ws, 7, 5, 7, 8)), ('ELL / CSR bytes', ref(ws, 10, 5, 10, 8))],
         'What drives ELL/CSR: K / avg', 'matrix', 'ratio', labels=True, ymin=0)
ws.add_chart(ch, f'{col_at_cm(ws, 17)}11')
ch = bar(ws, cats, [('ELL padding %', ref(ws, 11, 5, 11, 8))], 'Fraction of ELL slots that are padding', 'matrix',
         'padding fraction', labels=True, ymin=0)
ws.add_chart(ch, 'A29')
ws.freeze_panes = 'B5'

# ============================================================= 2c Load balance
ws = wb.create_sheet('2c_LoadBalance'); widths(ws, 20, 13, 30)
title(ws, '2c -- Load-balancing analysis (8 threads)',
      'A = modelled imbalance from the .mtx row lengths (not measured).  B-E = measured ms per SpMV.  Speedup = time(row/static) / time(config).')
put(ws, 'A3', 'A. Modelled imbalance: max / avg nnz per thread (1.00 = perfect)', SEC, border=False)
header(ws, 4, ['matrix', 'static, row-based', 'nnz-based', 'reduction (row / nnz)'])
for i, mat in enumerate(MATS):
    r = 5 + i
    put(ws, f'A{r}', mat, BOLD)
    put(ws, f'B{r}', pred[mat]['rowImb'], BLUE, '0.000'); put(ws, f'C{r}', pred[mat]['nnzImb'], BLUE, '0.000')
    put(ws, f'D{r}', f'=B{r}/C{r}', fmt='0.00"x"')
ch = bar(ws, ref(ws, 1, 5, 1, 8), [('static, row-based', ref(ws, 2, 5, 2, 8)), ('nnz-based', ref(ws, 3, 5, 3, 8))],
         'Thread imbalance (max/avg nnz per thread), modelled', 'matrix', 'max / avg', labels=True, ymin=0, w=15, h=7.5)
ws.add_chart(ch, 'G3')

CFGS = [c for c, _ in lb[('ecology2', 'csr-omp')]]
def block(r0, label, kernel):
    put(ws, f'A{r0}', label, SEC, border=False)
    header(ws, r0 + 1, ['config (partition/schedule)'] + MATS)
    for j, cfg in enumerate(CFGS):
        r = r0 + 2 + j; put(ws, f'A{r}', cfg, BOLD)
        for k, mat in enumerate(MATS):
            put(ws, f'{CL(2 + k)}{r}', lb[(mat, kernel)][j][1], BLUE, '0.0000')
TIME_ROW = {'csr-omp': 21, 'ell-omp': 30}
block(19, 'B. CSR -- time (ms) at 8 threads', 'csr-omp')
block(28, 'C. ELL -- time (ms) at 8 threads', 'ell-omp')
# speedup blocks: base row = first config row of the corresponding time block
def spd_block(r0, label, kernel):
    put(ws, f'A{r0}', label, SEC, border=False)
    header(ws, r0 + 1, ['config (partition/schedule)'] + MATS)
    base = TIME_ROW[kernel]
    for j, cfg in enumerate(CFGS):
        r = r0 + 2 + j; put(ws, f'A{r}', cfg, BOLD)
        for k in range(4):
            c = CL(2 + k)
            put(ws, f'{c}{r}', f'={c}${base}/{c}{base + j}', fmt='0.00"x"')
spd_block(37, 'D. CSR -- speedup vs row/static  (>1 = faster than static)', 'csr-omp')
spd_block(46, 'E. ELL -- speedup vs row/static', 'ell-omp')
for r0, kern, name in ((39, 'CSR', 'CSR'), (48, 'ELL', 'ELL')):
    ch = bar(ws, ref(ws, 1, r0, 1, r0 + 5), [(m, ref(ws, 2 + k, r0, 2 + k, r0 + 5)) for k, m in enumerate(MATS)],
             f'{name}: speedup of each schedule/partition vs row/static (8 threads)', 'partition / schedule',
             'speedup vs static/row', ymin=0, w=18, h=8.5)
    ws.add_chart(ch, 'G19' if r0 == 39 else 'G37')
ws.freeze_panes = 'B5'

# ================================================================ 2d Winner
ws = wb.create_sheet('2d_Winner'); widths(ws, 14, 13, 16)
title(ws, '2d / 2e(d) -- Winner per matrix at 8 threads, and prediction check',
      'best OpenMP config = fastest schedule x partition at 8 threads (all runs PASSed).  Prediction comes from 2b_Memory.')
header(ws, 4, ['matrix', 'CSR seq (ms)', 'ELL seq (ms)', 'best CSR-omp (ms)', 'best CSR config', 'best ELL-omp (ms)',
               'best ELL config', 'winner (sequential)', 'winner (8 threads)', 'margin (loser / winner)',
               'CSR speedup vs seq', 'ELL speedup vs seq', 'predicted winner (2b)', 'prediction correct?'])
for i, mat in enumerate(MATS):
    r = 5 + i; d = win[mat]
    put(ws, f'A{r}', mat, BOLD)
    put(ws, f'B{r}', d['cseq'], BLUE, '0.0000'); put(ws, f'C{r}', d['eseq'], BLUE, '0.0000')
    put(ws, f'D{r}', d['comp'], BLUE, '0.0000'); put(ws, f'E{r}', d['ccfg'], BLUE, align=CEN)
    put(ws, f'F{r}', d['eomp'], BLUE, '0.0000'); put(ws, f'G{r}', d['ecfg'], BLUE, align=CEN)
    put(ws, f'H{r}', f'=IF(B{r}<=C{r},"CSR","ELL")', BOLD, align=CEN)
    put(ws, f'I{r}', f'=IF(D{r}<=F{r},"CSR","ELL")', BOLD, align=CEN)
    put(ws, f'J{r}', f'=MAX(D{r},F{r})/MIN(D{r},F{r})', fmt='0.00"x"')
    put(ws, f'K{r}', f'=B{r}/D{r}', fmt='0.00"x"'); put(ws, f'L{r}', f'=C{r}/F{r}', fmt='0.00"x"')
    put(ws, f'M{r}', f"=INDEX('2b_Memory'!$N$5:$N$8,MATCH($A{r},'2b_Memory'!$A$5:$A$8,0))", BOLD, align=CEN)
    put(ws, f'N{r}', f'=IF(M{r}=I{r},"yes","NO - surprise")', BOLD, align=CEN)
cats = ref(ws, 1, 5, 1, 8)
ch = bar(ws, cats, [('CSR sequential', ref(ws, 2, 5, 2, 8)), ('ELL sequential', ref(ws, 3, 5, 3, 8)),
                    ('CSR OpenMP (8t, best)', ref(ws, 4, 5, 4, 8)), ('ELL OpenMP (8t, best)', ref(ws, 6, 5, 6, 8))],
         'Time per SpMV: sequential vs best 8-thread config', 'matrix', 'ms (log scale)', log=True)
ws.add_chart(ch, 'A11')
ch = bar(ws, cats, [('CSR speedup vs sequential', ref(ws, 11, 5, 11, 8)), ('ELL speedup vs sequential', ref(ws, 12, 5, 12, 8))],
         'Speedup of best 8-thread config over sequential', 'matrix', 'speedup (x)', labels=True, ymin=0)
ws.add_chart(ch, f'{col_at_cm(ws, 17)}11')
ch = bar(ws, cats, [('winner margin (loser / winner time)', ref(ws, 10, 5, 10, 8))],
         'How much the winning format wins by (8 threads)', 'matrix', 'x faster', labels=True, ymin=0)
ws.add_chart(ch, 'A29')
ws.freeze_panes = 'B5'

# ========================================================= 2d Scaling data
ws = wb.create_sheet('2d_ScalingData'); widths(ws, 12, 11, 16)
ws.column_dimensions['B'].width = 10; ws.column_dimensions['C'].width = 14
title(ws, '2d -- Strong scaling data: S(t) = T1/Tt (measured),  E(t) = S(t)/t (formula)',
      'nnz partition is schedule-independent -> one row "nnz/any" per kernel.')
put(ws, 'D4', 'threads t ->', BOLD); put(ws, 'D5', 'ideal ->', BOLD)
for k, t in enumerate([1, 2, 4, 8, 16]):
    put(ws, f'{CL(5 + k)}4', t, BLUE, '0'); put(ws, f'{CL(10 + k)}4', f'={CL(5 + k)}4', fmt='0')
    put(ws, f'{CL(5 + k)}5', f'={CL(5 + k)}4', fmt='0.0')
    put(ws, f'{CL(10 + k)}5', 1, BLUE, '0%')
ws['O5'].value = '<- ideal efficiency = 100% (constant)'; ws['O5'].font = NOTE
header(ws, 7, ['matrix', 'kernel', 'schedule', 'partition'] + [f'S({t})' for t in [1, 2, 4, 8, 16]] +
       [f'E({t})' for t in [1, 2, 4, 8, 16]])
ROW = {}; r = 8
for mat in MATS:
    for kern in ('csr-omp', 'ell-omp'):
        for lab, S in sc[(mat, kern)]:
            put(ws, f'A{r}', mat, BOLD); put(ws, f'B{r}', kern[:3].upper())
            part, sched = ('nnz', 'any') if lab == 'nnz/any' else ('row', lab.split('/')[1])
            put(ws, f'C{r}', sched); put(ws, f'D{r}', part)
            for k, s in enumerate(S):
                put(ws, f'{CL(5 + k)}{r}', s, BLUE, '0.00')
                put(ws, f'{CL(10 + k)}{r}', f'={CL(5 + k)}{r}/{CL(5 + k)}$4', fmt='0%')
            ROW[(mat, kern, lab)] = r; r += 1
ws.freeze_panes = 'E8'

# ========================================================= 2d Scaling charts
wd = ws
ws = wb.create_sheet('2d_ScalingCharts'); widths(ws, 10, 10, 30)
title(ws, '2d -- Scaling charts (data on 2d_ScalingData)',
      'Sections: (A) speedup, (B) efficiency -- CSR vs ELL, static/row vs nnz;  (C) CSR schedule sweep;  (D) ELL schedule sweep.')
cats = Reference(wd, min_col=5, max_col=9, min_row=4)
ideal_S = Reference(wd, min_col=5, max_col=9, min_row=5)
ideal_E = Reference(wd, min_col=10, max_col=14, min_row=5)
def rowref(mat, kern, lab, eff):
    rr = ROW[(mat, kern, lab)]; c0 = 10 if eff else 5
    return Reference(wd, min_col=c0, max_col=c0 + 4, min_row=rr)
X2 = col_at_cm(ws, 17.5); STEP = 18
def section(r0, text):
    put(ws, f'A{r0}', text, SEC, border=False)
def place(k, r0):   # k = 0..3 -> 2x2 grid below section title at row r0
    row = r0 + 1 + (k // 2) * STEP
    return f'A{row}' if k % 2 == 0 else f'{X2}{row}'
sections = [(3, 'A. Speedup S(t): CSR vs ELL, static/row vs nnz', False),
            (3 + 2 * STEP + 2, 'B. Parallel efficiency E(t) = S(t)/t: CSR vs ELL, static/row vs nnz', True)]
for r0, text, eff in sections:
    section(r0, text)
    for k, mat in enumerate(MATS):
        ser = [('CSR static/row', rowref(mat, 'csr-omp', 'row/static', eff)),
               ('CSR nnz', rowref(mat, 'csr-omp', 'nnz/any', eff)),
               ('ELL static/row', rowref(mat, 'ell-omp', 'row/static', eff)),
               ('ELL nnz', rowref(mat, 'ell-omp', 'nnz/any', eff))]
        ch = line(ws, cats, ser, f'{mat}: ' + ('efficiency' if eff else 'speedup'), 'threads',
                  'E(t)' if eff else 'S(t)', ideal=ideal_E if eff else ideal_S, w=17, h=8.5)
        ws.add_chart(ch, place(k, r0))
r0 = 3 + 4 * STEP + 4
for kern, text in (('csr-omp', 'C. CSR schedule sweep -- speedup S(t)'), ('ell-omp', 'D. ELL schedule sweep -- speedup S(t)')):
    section(r0, text)
    for k, mat in enumerate(MATS):
        ser = [(lab, rowref(mat, kern, lab, False)) for lab, _ in sc[(mat, kern)]]
        ch = line(ws, cats, ser, f'{mat} / {kern[:3].upper()}: schedule & partition sweep', 'threads', 'S(t)',
                  ideal=ideal_S, w=17, h=8.5)
        ws.add_chart(ch, place(k, r0))
    r0 += 2 * STEP + 2

# ============================================================ 2e Bandwidth
ws = wb.create_sheet('2e_Bandwidth'); widths(ws, 21, 12, 16)
title(ws, '2e(b) -- Throughput, bandwidth and roofline (8 threads for OpenMP columns)',
      'GB/s as printed by the program counts 12 B per stored entry INCLUDING ELL padding; "useful" GB/s discounts the padding.')
put(ws, 'A3', 'OI (flop/byte)', BOLD); put(ws, 'B3', 0.17, BLUE, '0.00'); put(ws, 'C3', 'from the assignment handout', NOTE, border=False)
put(ws, 'A4', 'Peak BW (GB/s)', BOLD); put(ws, 'B4', None, BLUE, '0.0', YFILL)
put(ws, 'C4', '<- ENTER your STREAM triad result for this node (yellow); until then peak-based cells show 0', NOTE, border=False)
put(ws, 'A5', 'Roofline (GFLOP/s)', BOLD); put(ws, 'B5', '=B3*B4', fmt='0.0;-0.0;"(enter peak)"')
put(ws, 'C5', 'OI x peak BW  (attainable GFLOP/s for a memory-bound kernel)', NOTE, border=False)
header(ws, 8, ['matrix', 'CSR seq GFLOP/s', 'CSR seq GB/s', 'ELL seq GFLOP/s', 'ELL seq GB/s', 'CSR omp GFLOP/s', 'CSR omp GB/s',
               'ELL omp GFLOP/s', 'ELL omp GB/s', 'ELL padding (2b)', 'ELL omp useful GB/s', 'Peak BW (from B4)',
               'CSR omp % of peak', 'ELL omp % of peak', 'CSR omp % of roofline'])
for i, mat in enumerate(MATS):
    r = 9 + i
    put(ws, f'A{r}', mat, BOLD)
    for k, v in enumerate(thr[mat]):
        put(ws, f'{CL(2 + k)}{r}', v, BLUE, '0.00' if k % 2 == 0 else '0.0')
    put(ws, f'J{r}', f"=INDEX('2b_Memory'!$K$5:$K$8,MATCH($A{r},'2b_Memory'!$A$5:$A$8,0))", fmt='0.0%')
    put(ws, f'K{r}', f'=I{r}*(1-J{r})', fmt='0.0'); put(ws, f'L{r}', '=$B$4', fmt='0.0')
    put(ws, f'M{r}', f'=IF($B$4>0,G{r}/$B$4,0)', fmt='0.0%'); put(ws, f'N{r}', f'=IF($B$4>0,I{r}/$B$4,0)', fmt='0.0%')
    put(ws, f'O{r}', f'=IF($B$5>0,F{r}/$B$5,0)', fmt='0.0%')
cats = ref(ws, 1, 9, 1, 12)
ch = bar(ws, cats, [('CSR seq', ref(ws, 2, 9, 2, 12)), ('ELL seq', ref(ws, 4, 9, 4, 12)),
                    ('CSR omp 8t', ref(ws, 6, 9, 6, 12)), ('ELL omp 8t', ref(ws, 8, 9, 8, 12))],
         'Useful throughput (GFLOP/s)', 'matrix', 'GFLOP/s')
ws.add_chart(ch, 'A15')
ch = bar(ws, cats, [('CSR seq', ref(ws, 3, 9, 3, 12)), ('ELL seq', ref(ws, 5, 9, 5, 12)),
                    ('CSR omp 8t', ref(ws, 7, 9, 7, 12)), ('ELL omp 8t', ref(ws, 9, 9, 9, 12))],
         'Reported memory bandwidth (GB/s, ELL includes padding)', 'matrix', 'GB/s')
ws.add_chart(ch, f'{col_at_cm(ws, 17)}15')
ch = bar(ws, cats, [('CSR omp GB/s', ref(ws, 7, 9, 7, 12)), ('ELL omp GB/s (incl. padding)', ref(ws, 9, 9, 9, 12)),
                    ('ELL omp useful GB/s', ref(ws, 11, 9, 11, 12)), ('Peak (STREAM, B4)', ref(ws, 12, 9, 12, 12))],
         'Achieved vs peak bandwidth at 8 threads', 'matrix', 'GB/s')
ws.add_chart(ch, 'A33')
ch = bar(ws, cats, [('CSR omp % of peak', ref(ws, 13, 9, 13, 12)), ('ELL omp % of peak', ref(ws, 14, 9, 14, 12))],
         'Fraction of peak bandwidth achieved (needs B4)', 'matrix', '% of peak', ymin=0)
ws.add_chart(ch, f'{col_at_cm(ws, 17)}33')

from openpyxl.worksheet.properties import PageSetupProperties
for w_ in wb.worksheets:
    w_.page_setup.orientation = 'landscape'
    w_.sheet_properties.pageSetUpPr = PageSetupProperties(fitToPage=True)
    w_.page_setup.fitToWidth = 1; w_.page_setup.fitToHeight = 0
wb.save(OUT); print('saved', OUT)
