// ============================================================================
// Config Store - Zustand state management for configuration
// Uses IndexedDB for persistent storage (not C++ backend)
// ============================================================================

import { create } from 'zustand';
import { Config, DEFAULT_CONFIG } from '../types';
import { configDB } from '../services/configDB';
import { invoke } from '../services/bridge';

interface ConfigStore {
  config: Config;
  loaded: boolean;

  /** Load config from IndexedDB */
  loadConfig: () => Promise<void>;

  /** Save config to IndexedDB (and optionally notify backend) */
  saveConfig: (partial: Partial<Config>) => Promise<void>;

  /** Reset config to defaults */
  resetConfig: () => Promise<void>;
}

export const useConfigStore = create<ConfigStore>((set, get) => ({
  config: { ...DEFAULT_CONFIG },
  loaded: false,

  loadConfig: async () => {
    try {
      console.log('[ConfigStore] Loading config from IndexedDB...');
      await configDB.init();
      const config = await configDB.loadConfig();
      console.log('[ConfigStore] Config loaded successfully:', JSON.stringify(config));
      set({ config, loaded: true });

      // Apply saved nickname to backend immediately on startup
      if (config.nickname && config.nickname.length > 0) {
        console.log('[ConfigStore] Applying saved nickname to backend:', config.nickname);
        try {
          await invoke('config.set', { nickname: config.nickname });
        } catch (e) {
          console.error('[ConfigStore] Failed to set nickname on backend:', e);
        }
      }

      // Apply saved ipRanges to backend on startup — scan each range for friends
      if (config.ipRanges && config.ipRanges.length > 0) {
        console.log('[ConfigStore] Applying saved ipRanges to backend:', config.ipRanges);
        try {
          await invoke('network.scan_ranges', { ipRanges: config.ipRanges });
        } catch (e) {
          console.error('[ConfigStore] Failed to sync ipRanges to backend:', e);
        }
      }

    } catch (err) {
      console.error('[ConfigStore] Failed to load config:', err);
      set({ loaded: true });
    }
  },

  saveConfig: async (partial) => {
    const newConfig = { ...get().config, ...partial };
    set({ config: newConfig });

    try {
      console.log('[ConfigStore] Saving config to IndexedDB:', JSON.stringify(partial));
      await configDB.saveConfig(partial);
      console.log('[ConfigStore] Config saved successfully');

      // Notify backend of nickname changes
      const nickname = partial.nickname;
      if (nickname !== undefined) {
        console.log('[ConfigStore] Notifying backend of nickname change:', nickname);
        await invoke('config.set', { nickname });
      }

      // Notify backend of ipRanges changes — trigger scan for friend discovery
      const ipRanges = partial.ipRanges;
      if (ipRanges !== undefined) {
        console.log('[ConfigStore] Notifying backend of ipRanges change:', ipRanges);
        await invoke('network.scan_ranges', { ipRanges });
      }
    } catch (err) {
      console.error('[ConfigStore] Failed to save config:', err);
    }
  },

  resetConfig: async () => {
    set({ config: { ...DEFAULT_CONFIG } });
    try {
      await configDB.clear();
    } catch (err) {
      console.error('Failed to reset config:', err);
    }
  },
}));
