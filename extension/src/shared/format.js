// Small presentation helpers shared by the popup and the toast.

// Replace every character of a code with a bullet, grouping long codes in
// threes so "123456" reads as "••• •••".
export function maskCode(code) {
  const s = String(code || '');
  if (!s) return '';
  const dots = '•'.repeat(s.length);
  if (s.length === 6) return dots.slice(0, 3) + ' ' + dots.slice(3);
  if (s.length === 8) return dots.slice(0, 4) + ' ' + dots.slice(4);
  return dots;
}

// Human-readable age of a timestamp: "just now", "45 s ago", "3 min ago",
// "2 h ago", or a locale date for anything older than a day.
export function formatAge(ts, now = Date.now()) {
  const t = Number(ts);
  if (!Number.isFinite(t) || t <= 0) return '';
  const diff = Math.max(0, now - t);
  const sec = Math.round(diff / 1000);
  if (sec < 10) return 'just now';
  if (sec < 60) return `${sec} s ago`;
  const min = Math.round(sec / 60);
  if (min < 60) return `${min} min ago`;
  const hr = Math.round(min / 60);
  if (hr < 24) return `${hr} h ago`;
  try {
    return new Date(t).toLocaleString();
  } catch {
    return `${Math.round(hr / 24)} d ago`;
  }
}
