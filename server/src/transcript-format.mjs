// Keep this function self-contained so the same formatter can run in the workspace.
export function formatTranscript(transcript) {
  function formatTimestamp(seconds) {
    const total = Math.floor(seconds);
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor(total % 3600 / 60);
    const rest = total % 60;
    const pad = value => String(value).padStart(2, '0');
    return (hours ? pad(hours) + ':' : '') + pad(minutes) + ':' + pad(rest);
  }

  const timed = transcript.timedAlternatives;
  const segments = timed || transcript.segments;
  const prefix = timed
    ? '【SSE 转录结果】\n' + transcript.segments.map(segment => segment.text).join('') + '\n\n【时间分段（可能不完整）】\n'
    : '';
  const notice = segments.some(segment => segment.timeSource === 'vad_estimate')
    ? '时间范围为估算，请结合录音核对。\n\n'
    : '';
  return prefix + notice + segments.map(segment => {
    const {startSeconds: start, endSeconds: end} = segment;
    const label = Number.isFinite(start) && Number.isFinite(end) && start >= 0 && end >= start
      ? '[' + formatTimestamp(start) + ' – ' + formatTimestamp(end) + ' · ' + (end - start).toFixed(1) + 's]'
      : '[时间戳不可用]';
    return label + '\n' + segment.text;
  }).join('\n\n');
}
