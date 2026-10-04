import { describe, it, expect } from 'vitest';
import { maskCode, formatAge } from '../src/shared/format.js';
import { fillOutcomeText } from '../src/popup/popup.js';

describe('maskCode', () => {
  it('masks every character and groups 6/8-digit codes', () => {
    expect(maskCode('123456')).toBe('••• •••');
    expect(maskCode('12345678')).toBe('•••• ••••');
    expect(maskCode('1234')).toBe('••••');
    expect(maskCode('ABC12')).toBe('•••••');
    expect(maskCode('')).toBe('');
  });
});

describe('formatAge', () => {
  const now = 1_700_000_000_000;
  it('describes recent timestamps in words', () => {
    expect(formatAge(now - 3_000, now)).toBe('just now');
    expect(formatAge(now - 45_000, now)).toBe('45 s ago');
    expect(formatAge(now - 3 * 60_000, now)).toBe('3 min ago');
    expect(formatAge(now - 2 * 3_600_000, now)).toBe('2 h ago');
  });
  it('returns empty for garbage', () => {
    expect(formatAge(undefined, now)).toBe('');
    expect(formatAge(0, now)).toBe('');
  });
});

describe('fillOutcomeText', () => {
  it('explains each background reason', () => {
    expect(fillOutcomeText({ filled: true })).toBe('Filled.');
    expect(fillOutcomeText({ filled: false, reason: 'no_code' })).toBe('No code to fill yet.');
    expect(fillOutcomeText({ filled: false, reason: 'no_content_script' })).toBe('Tether cannot run on this page.');
    expect(fillOutcomeText(undefined)).toBe('Nothing was filled.');
  });
});
