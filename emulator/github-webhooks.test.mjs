import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { GithubWebhookStore, webhookTest } from './github-webhooks.mjs';

const repository = { full_name: 'ananas-it/web' };

test('normalizes active and completed workflow runs', () => {
  const active = webhookTest.normalize('workflow_run', {
    repository,
    workflow_run: { id: 1, name: 'Deploy', head_branch: 'main', status: 'in_progress', created_at: '2026-07-23T00:00:00Z' },
  });
  assert.equal(active.status, 'in_progress');
  assert.equal(active.type, 'action');

  const completed = webhookTest.normalize('workflow_run', {
    repository,
    workflow_run: { id: 1, name: 'Deploy', head_branch: 'main', status: 'completed', conclusion: 'failure', updated_at: '2026-07-23T00:01:00Z' },
  });
  assert.equal(completed.status, 'completed');
  assert.equal(completed.conclusion, 'failure');
});

test('upserts PR state from pull_request and check_suite events', () => {
  const directory = mkdtempSync(join(tmpdir(), 'smalltv-hooks-'));
  const file = join(directory, 'events.json');
  const store = new GithubWebhookStore(file);
  const secret = 'test-secret-with-enough-entropy';
  const send = (event, delivery, payload) => {
    const rawBody = Buffer.from(JSON.stringify(payload));
    const signature = `sha256=${createHmac('sha256', secret).update(rawBody).digest('hex')}`;
    return store.ingest({ event, delivery, signature, rawBody, secret });
  };

  assert.equal(send('pull_request', 'one', {
    action: 'opened',
    repository,
    pull_request: { number: 42, title: 'Ship it', created_at: '2026-07-23T00:00:00Z', updated_at: '2026-07-23T00:00:00Z', head: { ref: 'feature' } },
  }).status, 202);
  assert.equal(send('check_suite', 'two', {
    action: 'completed',
    repository,
    check_suite: { status: 'completed', conclusion: 'success', head_branch: 'feature', updated_at: '2026-07-23T00:02:00Z', pull_requests: [{ number: 42 }] },
  }).status, 202);

  const events = store.events({ owners: ['ananas-it'] });
  assert.equal(events.length, 1);
  assert.equal(events[0].conclusion, 'success');
  assert.equal(JSON.parse(readFileSync(file, 'utf8')).deliveries.length, 2);
});

test('rejects invalid signatures and deduplicates deliveries', () => {
  const directory = mkdtempSync(join(tmpdir(), 'smalltv-hooks-'));
  const store = new GithubWebhookStore(join(directory, 'events.json'));
  const secret = 'another-test-secret';
  const rawBody = Buffer.from(JSON.stringify({ zen: 'Keep it logically awesome.' }));
  assert.equal(store.ingest({ event: 'ping', delivery: 'ping-1', signature: 'sha256=bad', rawBody, secret }).status, 401);
  const signature = `sha256=${createHmac('sha256', secret).update(rawBody).digest('hex')}`;
  assert.equal(store.ingest({ event: 'ping', delivery: 'ping-1', signature, rawBody, secret }).status, 202);
  assert.equal(store.ingest({ event: 'ping', delivery: 'ping-1', signature, rawBody, secret }).body.duplicate, true);
});
