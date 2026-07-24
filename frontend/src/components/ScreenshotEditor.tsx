// ============================================================================
// ScreenshotEditor - full-screen screenshot region selector + annotation editor
//
// Flow (driven by ChatPanel):
//   1. ChatPanel calls `screenshot.capture` (C++ hides the window, captures the
//      current monitor, returns a base64 PNG, then re-shows the window maximised).
//   2. ChatPanel renders <ScreenshotEditor> with that image.
//   3. User drags to select a region, then annotates (rect / arrow / text / mosaic).
//   4. Confirm -> onConfirm(dataUrl) (ChatPanel sends it as an image message).
//      Save    -> writes the image to a file chosen via the native save dialog.
//      Cancel  -> onCancel() (ChatPanel restores the window).
// ============================================================================

import React, { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { invoke } from '../services/bridge';

type Tool = 'rect' | 'arrow' | 'text' | 'mosaic';

interface RectAnno  { type: 'rect';  x: number; y: number; w: number; h: number; color: string; width: number; }
interface ArrowAnno { type: 'arrow'; x1: number; y1: number; x2: number; y2: number; color: string; width: number; }
interface TextAnno  { type: 'text';  x: number; y: number; text: string; color: string; size: number; }
interface MosaicAnno{ type: 'mosaic';x: number; y: number; w: number; h: number; block: number; }
type Annotation = RectAnno | ArrowAnno | TextAnno | MosaicAnno;

interface ScreenshotEditorProps {
  image: string;                 // data URL of the captured monitor
  screenCount?: number;          // number of monitors
  onCancel: () => void;
  onConfirm: (dataUrl: string) => void;
}

const PRESET_COLORS = ['#ff3b30', '#ff9500', '#ffcc00', '#34c759', '#007aff', '#ffffff', '#000000'];

function blobToDataUrl(blob: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(r.result as string);
    r.onerror = reject;
    r.readAsDataURL(blob);
  });
}

export default function ScreenshotEditor({ image, screenCount, onCancel, onConfirm }: ScreenshotEditorProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const baseCanvasRef = useRef<HTMLCanvasElement | null>(null);

  const [loaded, setLoaded] = useState(false);
  const [phase, setPhase] = useState<'select' | 'edit'>('select');
  const [sel, setSel] = useState<{ x: number; y: number; w: number; h: number } | null>(null);
  const [annotations, setAnnotations] = useState<Annotation[]>([]);
  const [current, setCurrent] = useState<Annotation | null>(null);
  const [tool, setTool] = useState<Tool | null>('rect');
  const [color, setColor] = useState('#ff3b30');
  const [strokeWidth, setStrokeWidth] = useState(3);
  const [textInput, setTextInput] = useState<{ x: number; y: number; value: string; visible: boolean }>({ x: 0, y: 0, value: '', visible: false });

  const dragStartRef = useRef<{ x: number; y: number } | null>(null);
  const textInputRef = useRef<HTMLInputElement>(null);

  // ---- Load the captured image into an offscreen base canvas ----
  useEffect(() => {
    const img = new Image();
    img.onload = () => {
      const c = document.createElement('canvas');
      c.width = img.naturalWidth;
      c.height = img.naturalHeight;
      c.getContext('2d')!.drawImage(img, 0, 0);
      baseCanvasRef.current = c;
      setLoaded(true);
    };
    img.src = image;
  }, [image]);

  // ---- Natural (canvas pixel) size for the current phase ----
  const getNatural = useCallback((): { w: number; h: number } => {
    if (phase === 'edit' && sel) return { w: Math.max(1, Math.round(sel.w)), h: Math.max(1, Math.round(sel.h)) };
    const img = baseCanvasRef.current;
    return img ? { w: img.width, h: img.height } : { w: 1, h: 1 };
  }, [phase, sel]);

  // ---- Convert a mouse event to canvas-natural coordinates ----
  const toNatural = useCallback((e: React.MouseEvent): { x: number; y: number } => {
    const canvas = canvasRef.current!;
    const rect = canvas.getBoundingClientRect();
    return {
      x: ((e.clientX - rect.left) / rect.width) * canvas.width,
      y: ((e.clientY - rect.top) / rect.height) * canvas.height,
    };
  }, []);

  // ---- Draw a single annotation ----
  const drawAnno = useCallback((ctx: CanvasRenderingContext2D, a: Annotation, scale: number, s: { x: number; y: number }, base: HTMLCanvasElement) => {
    if (a.type === 'rect') {
      ctx.strokeStyle = a.color;
      ctx.lineWidth = Math.max(1, a.width * scale);
      ctx.strokeRect(a.x, a.y, a.w, a.h);
    } else if (a.type === 'arrow') {
      ctx.strokeStyle = a.color;
      ctx.lineWidth = Math.max(1, a.width * scale);
      ctx.beginPath();
      ctx.moveTo(a.x1, a.y1);
      ctx.lineTo(a.x2, a.y2);
      ctx.stroke();
      const ang = Math.atan2(a.y2 - a.y1, a.x2 - a.x1);
      const head = Math.max(12, a.width * scale * 3.5);
      const a1 = ang + Math.PI * 0.85;
      const a2 = ang - Math.PI * 0.85;
      ctx.beginPath();
      ctx.moveTo(a.x2, a.y2);
      ctx.lineTo(a.x2 + head * Math.cos(a1), a.y2 + head * Math.sin(a1));
      ctx.moveTo(a.x2, a.y2);
      ctx.lineTo(a.x2 + head * Math.cos(a2), a.y2 + head * Math.sin(a2));
      ctx.stroke();
    } else if (a.type === 'text') {
      ctx.fillStyle = a.color;
      ctx.font = `${Math.max(8, a.size * scale)}px sans-serif`;
      ctx.textBaseline = 'top';
      ctx.fillText(a.text, a.x, a.y);
    } else if (a.type === 'mosaic') {
      const bw = Math.max(4, Math.round(a.block * scale));
      const tmpW = Math.max(1, Math.floor(a.w / bw));
      const tmpH = Math.max(1, Math.floor(a.h / bw));
      const tmp = document.createElement('canvas');
      tmp.width = tmpW;
      tmp.height = tmpH;
      const tctx = tmp.getContext('2d')!;
      tctx.imageSmoothingEnabled = false;
      // Pixelate from the original screenshot region (sel is in full-image coords).
      tctx.drawImage(base, s.x + a.x, s.y + a.y, a.w, a.h, 0, 0, tmpW, tmpH);
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(tmp, 0, 0, tmpW, tmpH, a.x, a.y, a.w, a.h);
      ctx.imageSmoothingEnabled = true;
    }
  }, []);

  // ---- Render the canvas ----
  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    const base = baseCanvasRef.current;
    if (!canvas || !base) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const rect = canvas.getBoundingClientRect();
    const scale = canvas.width / (rect.width || canvas.width);

    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (phase === 'select') {
      ctx.drawImage(base, 0, 0, canvas.width, canvas.height);
      if (sel && sel.w > 0 && sel.h > 0) {
        ctx.fillStyle = 'rgba(0,0,0,0.45)';
        const { x, y, w, h } = sel;
        ctx.fillRect(0, 0, canvas.width, y);
        ctx.fillRect(0, y + h, canvas.width, canvas.height - y - h);
        ctx.fillRect(0, y, x, h);
        ctx.fillRect(x + w, y, canvas.width - x - w, h);
        ctx.strokeStyle = '#1e90ff';
        ctx.lineWidth = Math.max(1, 2 * scale);
        ctx.strokeRect(x, y, w, h);
      }
    } else if (sel) {
      const s = sel;
      ctx.drawImage(base, s.x, s.y, s.w, s.h, 0, 0, canvas.width, canvas.height);
      const list = current ? [...annotations, current] : annotations;
      for (const a of list) drawAnno(ctx, a, scale, s, base);
    }
  }, [phase, sel, annotations, current, drawAnno]);

  // ---- Layout: size the canvas + wrapper, then draw ----
  useLayoutEffect(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    const wrap = wrapRef.current;
    const base = baseCanvasRef.current;
    if (!canvas || !container || !wrap || !base || !loaded) return;

    const nat = getNatural();
    canvas.width = nat.w;
    canvas.height = nat.h;

    const cw = container.clientWidth;
    const ch = container.clientHeight;
    const ar = nat.w / nat.h;
    let fw = cw;
    let fh = cw / ar;
    if (fh > ch) { fh = ch; fw = ch * ar; }
    wrap.style.width = Math.floor(fw) + 'px';
    wrap.style.height = Math.floor(fh) + 'px';
    canvas.style.width = '100%';
    canvas.style.height = '100%';

    draw();
  }, [loaded, phase, sel, annotations, current, color, strokeWidth, textInput, getNatural, draw]);

  // Re-fit on window resize.
  useEffect(() => {
    const onResize = () => {
      const canvas = canvasRef.current;
      const container = containerRef.current;
      const wrap = wrapRef.current;
      const base = baseCanvasRef.current;
      if (!canvas || !container || !wrap || !base || !loaded) return;
      const nat = getNatural();
      const cw = container.clientWidth;
      const ch = container.clientHeight;
      const ar = nat.w / nat.h;
      let fw = cw;
      let fh = cw / ar;
      if (fh > ch) { fh = ch; fw = ch * ar; }
      wrap.style.width = Math.floor(fw) + 'px';
      wrap.style.height = Math.floor(fh) + 'px';
    };
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, [loaded, phase, sel, getNatural]);

  // ---- Mouse handling ----
  const onMouseDown = (e: React.MouseEvent) => {
    const p = toNatural(e);
    if (phase === 'select') {
      dragStartRef.current = p;
      setSel(null);
    } else if (tool) {
      if (tool === 'text') {
        setTextInput({ x: p.x, y: p.y, value: '', visible: true });
        setTimeout(() => textInputRef.current?.focus(), 0);
        return;
      }
      dragStartRef.current = p;
      if (tool === 'rect') setCurrent({ type: 'rect', x: p.x, y: p.y, w: 0, h: 0, color, width: strokeWidth });
      else if (tool === 'arrow') setCurrent({ type: 'arrow', x1: p.x, y1: p.y, x2: p.x, y2: p.y, color, width: strokeWidth });
      else if (tool === 'mosaic') setCurrent({ type: 'mosaic', x: p.x, y: p.y, w: 0, h: 0, block: Math.max(8, strokeWidth * 4) });
    }
  };

  const onMouseMove = (e: React.MouseEvent) => {
    const start = dragStartRef.current;
    if (!start) return;
    const p = toNatural(e);
    if (phase === 'select') {
      setSel({
        x: Math.min(start.x, p.x),
        y: Math.min(start.y, p.y),
        w: Math.abs(p.x - start.x),
        h: Math.abs(p.y - start.y),
      });
    } else if (current) {
      if (current.type === 'rect') {
        setCurrent({ ...current, x: Math.min(start.x, p.x), y: Math.min(start.y, p.y), w: Math.abs(p.x - start.x), h: Math.abs(p.y - start.y) });
      } else if (current.type === 'mosaic') {
        setCurrent({ ...current, x: Math.min(start.x, p.x), y: Math.min(start.y, p.y), w: Math.abs(p.x - start.x), h: Math.abs(p.y - start.y) });
      } else if (current.type === 'arrow') {
        setCurrent({ ...current, x2: p.x, y2: p.y });
      }
    }
  };

  const onMouseUp = () => {
    const start = dragStartRef.current;
    dragStartRef.current = null;
    if (phase === 'select') {
      if (sel && sel.w > 5 && sel.h > 5) {
        setPhase('edit');
        setAnnotations([]);
      }
    } else if (current) {
      const c = current;
      let tiny = false;
      if (c.type === 'rect' || c.type === 'mosaic') tiny = c.w < 3 && c.h < 3;
      else if (c.type === 'arrow') tiny = Math.hypot((c as ArrowAnno).x2 - (c as ArrowAnno).x1, (c as ArrowAnno).y2 - (c as ArrowAnno).y1) < 3;
      if (!tiny) setAnnotations((a) => [...a, c]);
      setCurrent(null);
    }
    void start;
  };

  const commitText = () => {
    if (textInput.value.trim()) {
      setAnnotations((a) => [...a, { type: 'text', x: textInput.x, y: textInput.y, text: textInput.value, color, size: Math.max(16, strokeWidth * 6) }]);
    }
    setTextInput({ ...textInput, visible: false, value: '' });
  };

  // ---- Confirm / Save / Cancel ----
  const exportCanvas = useCallback((): Promise<string | null> => {
    const canvas = canvasRef.current;
    if (!canvas) return Promise.resolve(null);
    return new Promise((resolve) => {
      canvas.toBlob((blob) => {
        if (!blob) { resolve(null); return; }
        blobToDataUrl(blob).then(resolve);
      }, 'image/png');
    });
  }, []);

  const handleConfirm = async () => {
    const dataUrl = await exportCanvas();
    if (dataUrl) onConfirm(dataUrl);
  };

  const handleSave = async () => {
    const dataUrl = await exportCanvas();
    if (!dataUrl) return;
    const base64 = dataUrl.split(',')[1];
    try {
      const res: any = await invoke('dialog.save', { title: '保存截图', default_name: 'screenshot.png' });
      if (res && res.success && res.path) {
        const sres: any = await invoke('file.save_data', { data: base64, path: res.path });
        if (!sres || !sres.success) alert('保存失败: ' + ((sres && sres.error) || '未知错误'));
      }
    } catch (err) {
      alert('保存失败: ' + err);
    }
  };

  // Esc cancels (unless editing text).
  useEffect(() => {
    const h = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !textInput.visible) onCancel();
    };
    window.addEventListener('keydown', h);
    return () => window.removeEventListener('keydown', h);
  }, [textInput.visible, onCancel]);

  const isEditing = phase === 'edit';
  const canvasRect = canvasRef.current?.getBoundingClientRect();
  const scaleX = canvasRect && canvasRef.current ? canvasRect.width / canvasRef.current.width : 1;
  const scaleY = canvasRect && canvasRef.current ? canvasRect.height / canvasRef.current.height : 1;

  return (
    <div
      style={{
        position: 'fixed',
        inset: 0,
        zIndex: 99999,
        background: '#1b1b1b',
        display: 'flex',
        flexDirection: 'column',
        userSelect: 'none',
        fontFamily: 'system-ui, sans-serif',
      }}
    >
      <style>{`
        .ss-btn {
          padding: 5px 14px; border-radius: 4px; cursor: pointer;
          border: 1px solid #666; background: #3a3a3a; color: #eee; font-size: 13px;
        }
        .ss-btn:hover:not(:disabled) { background: #4a4a4a; }
        .ss-btn:disabled { opacity: 0.4; cursor: default; }
        .ss-cancel:hover:not(:disabled) { background: #6b2b2b; border-color: #a44; }
        .ss-ok { background: #1e90ff; border-color: #1e90ff; color: #fff; }
        .ss-ok:hover:not(:disabled) { background: #0f7fe0; }
      `}</style>

      {/* Top toolbar */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 10,
          padding: '8px 12px',
          background: '#2b2b2b',
          color: '#eee',
          flexWrap: 'wrap',
          borderBottom: '1px solid #000',
        }}
      >
        {!isEditing && <span style={{ fontWeight: 600 }}>拖动鼠标选择要截取的屏幕区域</span>}
        {isEditing && (
          <>
            <ToolButton active={tool === 'rect'}  onClick={() => setTool('rect')}  label="矩形" />
            <ToolButton active={tool === 'arrow'} onClick={() => setTool('arrow')} label="箭头" />
            <ToolButton active={tool === 'text'} onClick={() => setTool('text')} label="文字" />
            <ToolButton active={tool === 'mosaic'}onClick={() => setTool('mosaic')}label="马赛克" />
            <span style={{ width: 1, height: 22, background: '#555' }} />
            <div style={{ display: 'flex', gap: 4 }}>
              {PRESET_COLORS.map((c) => (
                <button
                  key={c}
                  onClick={() => setColor(c)}
                  title={c}
                  style={{
                    width: 20, height: 20, borderRadius: '50%',
                    background: c, cursor: 'pointer',
                    border: color === c ? '2px solid #fff' : '1px solid #777',
                  }}
                />
              ))}
            </div>
            <span style={{ width: 1, height: 22, background: '#555' }} />
            <label style={{ fontSize: 12, opacity: 0.8 }}>粗细</label>
            <input type="range" min={1} max={12} value={strokeWidth} onChange={(e) => setStrokeWidth(Number(e.target.value))} />
            <button className="ss-btn" onClick={() => setAnnotations((a) => a.slice(0, -1))} disabled={annotations.length === 0}>撤销</button>
            <button className="ss-btn" onClick={() => { setPhase('select'); setSel(null); setAnnotations([]); setCurrent(null); }}>重新选择</button>
          </>
        )}
        <div style={{ flex: 1 }} />
        <button className="ss-btn" onClick={handleSave} disabled={!isEditing}>保存</button>
        <button className="ss-btn ss-cancel" onClick={onCancel}>取消</button>
        <button className="ss-btn ss-ok" onClick={handleConfirm} disabled={!isEditing}>确定</button>
      </div>

      {/* Canvas area */}
      <div
        ref={containerRef}
        style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', position: 'relative', overflow: 'hidden' }}
      >
        <div ref={wrapRef} style={{ position: 'relative', lineHeight: 0 }}>
          <canvas
            ref={canvasRef}
            style={{ display: 'block', cursor: isEditing && tool ? 'crosshair' : 'default', touchAction: 'none' }}
            onMouseDown={onMouseDown}
            onMouseMove={onMouseMove}
            onMouseUp={onMouseUp}
            onMouseLeave={onMouseUp}
            onDragStart={(e) => e.preventDefault()}
          />
          {textInput.visible && (
            <input
              ref={textInputRef}
              value={textInput.value}
              autoFocus
              onChange={(e) => setTextInput({ ...textInput, value: e.target.value })}
              onBlur={commitText}
              onKeyDown={(e) => {
                if (e.key === 'Enter') commitText();
                if (e.key === 'Escape') setTextInput({ ...textInput, visible: false, value: '' });
              }}
              style={{
                position: 'absolute',
                left: textInput.x * scaleX,
                top: textInput.y * scaleY,
                fontSize: Math.max(14, strokeWidth * 6 * scaleX),
                color,
                background: 'rgba(255,255,255,0.85)',
                border: '1px solid #1e90ff',
                outline: 'none',
                padding: '2px 4px',
                fontFamily: 'sans-serif',
                zIndex: 2,
              }}
            />
          )}
        </div>
      </div>

      {screenCount && screenCount > 1 && (
        <div style={{ position: 'absolute', bottom: 12, left: 12, color: '#aaa', fontSize: 12, background: 'rgba(0,0,0,0.4)', padding: '4px 8px', borderRadius: 4 }}>
          已截取当前屏幕（共 {screenCount} 屏）
        </div>
      )}
    </div>
  );
}

function ToolButton({ active, onClick, label }: { active: boolean; onClick: () => void; label: string }) {
  return (
    <button
      onClick={onClick}
      style={{
        padding: '4px 10px',
        borderRadius: 4,
        cursor: 'pointer',
        border: '1px solid ' + (active ? '#1e90ff' : '#666'),
        background: active ? '#1e90ff' : '#3a3a3a',
        color: active ? '#fff' : '#ddd',
        fontSize: 13,
      }}
    >
      {label}
    </button>
  );
}
