import { createHmac, timingSafeEqual } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';

const ACTIVE = new Set(['requested', 'queued', 'waiting', 'pending', 'in_progress']);
const FAILURE = new Set(['failure', 'failed', 'error', 'timed_out', 'action_required', 'startup_failure']);
const CANCELLED = new Set(['cancelled', 'canceled', 'skipped', 'stale', 'neutral', 'inactive']);

function normalizedState(status = '', conclusion = '') {
  const rawStatus = String(status || '').toLowerCase();
  const rawConclusion = String(conclusion || '').toLowerCase();
  if (ACTIVE.has(rawStatus)) {
    return {
      status: rawStatus === 'requested' || rawStatus === 'waiting' ? 'queued' : rawStatus,
      conclusion: '',
    };
  }
  if (FAILURE.has(rawConclusion) || FAILURE.has(rawStatus)) return { status: 'completed', conclusion: 'failure' };
  if (CANCELLED.has(rawConclusion) || CANCELLED.has(rawStatus)) return { status: 'completed', conclusion: 'cancelled' };
  if (rawConclusion === 'success' || rawStatus === 'success') return { status: 'completed', conclusion: 'success' };
  return { status: rawStatus === 'completed' ? 'completed' : 'queued', conclusion: rawConclusion };
}

function repositoryName(payload) {
  return String(payload.repository?.full_name || '').trim();
}

function normalize(event, payload) {
  const repo = repositoryName(payload);
  if (!repo) return null;

  if (event === 'workflow_run') {
    const run = payload.workflow_run;
    if (!run) return null;
    return {
      key: `workflow_run:${repo}:${run.id}`,
      repo,
      type: 'action',
      workflow: run.name || run.display_title || 'GitHub Actions',
      branch: run.head_branch || '',
      ...normalizedState(run.status, run.conclusion),
      occurredAt: run.updated_at || run.run_started_at || run.created_at,
      startedAt: run.run_started_at || run.created_at,
    };
  }

  if (event === 'deployment') {
    const deployment = payload.deployment;
    if (!deployment) return null;
    return {
      key: `deployment:${repo}:${deployment.id}`,
      repo,
      type: 'deployment',
      workflow: deployment.environment || payload.environment || 'deployment',
      branch: deployment.ref || '',
      status: 'queued',
      conclusion: '',
      occurredAt: deployment.updated_at || deployment.created_at,
      startedAt: deployment.created_at,
    };
  }

  if (event === 'deployment_status') {
    const deployment = payload.deployment;
    const deploymentStatus = payload.deployment_status;
    if (!deployment || !deploymentStatus) return null;
    return {
      key: `deployment:${repo}:${deployment.id}`,
      repo,
      type: 'deployment',
      workflow: deploymentStatus.environment || deployment.environment || 'deployment',
      branch: deployment.ref || '',
      ...normalizedState(deploymentStatus.state, deploymentStatus.state),
      occurredAt: deploymentStatus.updated_at || deploymentStatus.created_at || deployment.updated_at,
      startedAt: deployment.created_at,
    };
  }

  if (event === 'pull_request') {
    const pr = payload.pull_request;
    if (!pr) return null;
    const closed = payload.action === 'closed';
    const state = closed
      ? { status: 'completed', conclusion: pr.merged ? 'success' : 'cancelled' }
      : { status: payload.action === 'converted_to_draft' ? 'waiting' : 'queued', conclusion: '' };
    return {
      key: `pull_request:${repo}:${pr.number}`,
      repo,
      type: 'pull_request',
      workflow: `PR #${pr.number} ${pr.title || ''}`.trim(),
      branch: pr.head?.ref || '',
      ...state,
      occurredAt: pr.updated_at || pr.created_at,
      startedAt: pr.created_at,
    };
  }

  if (event === 'check_suite') {
    const suite = payload.check_suite;
    const pr = suite?.pull_requests?.[0];
    if (!suite || !pr) return null;
    return {
      key: `pull_request:${repo}:${pr.number}`,
      repo,
      type: 'pull_request',
      workflow: `PR #${pr.number} checks`,
      branch: suite.head_branch || pr.head?.ref || '',
      ...normalizedState(suite.status, suite.conclusion),
      occurredAt: suite.updated_at || suite.completed_at || suite.started_at || suite.created_at,
      startedAt: suite.started_at || suite.created_at,
    };
  }

  if (event === 'release' && ['published', 'released'].includes(payload.action)) {
    const release = payload.release;
    if (!release) return null;
    return {
      key: `release:${repo}:${release.id}`,
      repo,
      type: 'release',
      workflow: `Release ${release.tag_name || release.name || ''}`.trim(),
      branch: release.target_commitish || '',
      status: 'completed',
      conclusion: 'success',
      occurredAt: release.published_at || release.created_at,
      startedAt: release.created_at,
    };
  }

  return null;
}

function safeState(file) {
  try {
    const parsed = JSON.parse(readFileSync(file, 'utf8'));
    if (parsed && Array.isArray(parsed.events) && Array.isArray(parsed.deliveries)) return parsed;
  } catch {}
  return { version: 1, updatedAt: 0, events: [], deliveries: [] };
}

export class GithubWebhookStore {
  constructor(file, { record = () => {} } = {}) {
    this.file = file;
    this.record = record;
    this.state = safeState(file);
  }

  verify(rawBody, signature, secret) {
    if (!secret || !signature?.startsWith('sha256=')) return false;
    const expected = `sha256=${createHmac('sha256', secret).update(rawBody).digest('hex')}`;
    const expectedBuffer = Buffer.from(expected);
    const actualBuffer = Buffer.from(signature);
    return expectedBuffer.length === actualBuffer.length && timingSafeEqual(expectedBuffer, actualBuffer);
  }

  ingest({ event, delivery, signature, rawBody, secret }) {
    if (!secret) return { status: 503, body: { ok: false, error: { code: 'WEBHOOK_SECRET_MISSING', message: 'Configure a webhook secret in GitHub settings.' } } };
    if (!this.verify(rawBody, signature, secret)) return { status: 401, body: { ok: false, error: { code: 'WEBHOOK_SIGNATURE_INVALID', message: 'X-Hub-Signature-256 validation failed.' } } };
    if (!delivery) return { status: 400, body: { ok: false, error: { code: 'WEBHOOK_DELIVERY_MISSING', message: 'X-GitHub-Delivery is required.' } } };
    if (this.state.deliveries.includes(delivery)) return { status: 202, body: { ok: true, duplicate: true, delivery } };

    let payload;
    try { payload = JSON.parse(rawBody.toString('utf8')); }
    catch { return { status: 400, body: { ok: false, error: { code: 'WEBHOOK_JSON_INVALID', message: 'Webhook body is not valid JSON.' } } }; }

    this.state.deliveries.push(delivery);
    this.state.deliveries = this.state.deliveries.slice(-1000);
    const normalized = normalize(event, payload);
    if (normalized) {
      const receivedAt = Date.now();
      const next = { ...normalized, receivedAt, delivery };
      const index = this.state.events.findIndex(item => item.key === next.key);
      if (index >= 0) this.state.events[index] = next;
      else this.state.events.push(next);
      const cutoff = receivedAt - 14 * 24 * 60 * 60 * 1000;
      this.state.events = this.state.events
        .filter(item => ACTIVE.has(item.status) || Number(item.receivedAt || 0) >= cutoff)
        .sort((a, b) => Number(b.receivedAt || 0) - Number(a.receivedAt || 0))
        .slice(0, 1000);
    }
    this.state.updatedAt = Date.now();
    this.save();
    this.record('info', 'github.webhook.received', {
      event,
      delivery,
      repo: normalized?.repo || repositoryName(payload),
      message: normalized ? `${event}: ${normalized.status}/${normalized.conclusion || '-'}` : `${event}: ignored`,
    });
    return { status: 202, body: { ok: true, accepted: Boolean(normalized), event, delivery } };
  }

  save() {
    writeFileSync(this.file, `${JSON.stringify(this.state, null, 2)}\n`, { mode: 0o600 });
  }

  status(secretSet = false) {
    const latest = this.state.events[0];
    return {
      secretSet,
      received: this.state.deliveries.length,
      tracked: this.state.events.length,
      lastDeliveryAt: this.state.updatedAt || 0,
      lastEvent: latest ? { repo: latest.repo, type: latest.type, status: latest.status } : null,
    };
  }

  events({ repositories = [], owners = [], enabled = {}, limit = 16 } = {}) {
    const repositorySet = new Set(repositories.map(value => String(value).toLowerCase()));
    const ownerSet = new Set(owners.map(value => String(value).toLowerCase()));
    const typeSetting = { action: 'actions', deployment: 'deployments', pull_request: 'pullRequests', release: 'releases' };
    const allowed = this.state.events.filter(event => {
      if (enabled[typeSetting[event.type]] === false) return false;
      const repo = event.repo.toLowerCase();
      if (repositorySet.size) return repositorySet.has(repo);
      if (ownerSet.size) return ownerSet.has(repo.split('/')[0]);
      return true;
    });
    const priority = event => ACTIVE.has(event.status) ? 0 : event.conclusion === 'failure' ? 1 : 2;
    return allowed
      .sort((a, b) => priority(a) - priority(b) || Number(b.receivedAt || 0) - Number(a.receivedAt || 0))
      .slice(0, limit);
  }
}

export const webhookTest = { normalize, normalizedState };
