import { describe, it, expect, vi, afterEach } from 'vitest';
import { createMailMonitor } from '../src/mail/extractor.js';

const now = 1700000000000;
const message = id => ({ id, date: new Date(now), subject: 'Your login code', author: 'security@example.com' });
function setup(process = vi.fn().mockResolvedValue()) {
  const api = { messages: {
    query: vi.fn().mockResolvedValue({ messages: [] }),
    continueList: vi.fn().mockResolvedValue({ messages: [message(2)] })
  } };
  return { api, process, monitor: createMailMonitor(api, process, () => now) };
}
afterEach(() => vi.useRealTimers());
describe('mail monitor without mailbox polling', () => {
  it('does no startup or recurring queries when idle', async () => {
    vi.useFakeTimers();
    const { api, monitor } = setup();
    await vi.advanceTimersByTimeAsync(120000);
    await monitor.idle();
    expect(api.messages.query).not.toHaveBeenCalled();
  });
  it('walks event pages and deduplicates display/event overlap', async () => {
    const { api, process, monitor } = setup();
    monitor.received({}, { messages: [message(1)], id: 'next' });
    await monitor.displayed(1, message(2));
    expect(api.messages.continueList).toHaveBeenCalledWith('next');
    expect(process.mock.calls.map(([m]) => m.id)).toEqual([1, 2]);
    expect(api.messages.query).not.toHaveBeenCalled();
  });
  it('ignores ordinary folder changes and debounces new mail to its folder', async () => {
    vi.useFakeTimers();
    const { api, monitor } = setup();
    monitor.folderChanged({ id: 'inbox' }, { unreadMessageCount: 12 });
    monitor.folderChanged({}, { newMessageCount: 1 });
    expect(vi.getTimerCount()).toBe(0);
    monitor.folderChanged({ id: 'inbox' }, { newMessageCount: 1 });
    monitor.folderChanged({ id: 'inbox' }, { newMessageCount: 2 });
    await vi.advanceTimersByTimeAsync(1000);
    await monitor.idle();
    expect(api.messages.query).toHaveBeenCalledExactlyOnceWith({ folderId: 'inbox', includeSubFolders: false, fromDate: new Date(now - 600000) });
  });
  it('serializes body reads even across concurrent events', async () => {
    let release;
    const process = vi.fn().mockImplementationOnce(() => new Promise(r => { release = r; })).mockResolvedValue();
    const { monitor } = setup(process);
    monitor.received({}, { messages: [message(1)] });
    const done = monitor.received({}, { messages: [message(2)] });
    await Promise.resolve();
    expect(process).toHaveBeenCalledTimes(1);
    release();
    await done;
    expect(process).toHaveBeenCalledTimes(2);
  });
  it('rejects expired messages including displayed mail', async () => {
    const { process, monitor } = setup();
    await monitor.displayed(1, { ...message(1), date: new Date(now - 600001) });
    expect(process).not.toHaveBeenCalled();
  });
});
