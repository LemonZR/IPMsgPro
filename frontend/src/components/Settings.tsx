import React, { useState, useEffect } from 'react';
import { FiX, FiPlus, FiTrash2, FiFolder, FiRotateCcw } from 'react-icons/fi';
import { useConfigStore } from '../stores/configStore';
import { Config, DEFAULT_CONFIG } from '../types';
import { invoke } from '../services/bridge';

interface SettingsProps {
  onClose: () => void;
}

/** Default data directory under user home */
const getDefaultDataDir = (): string => {
  // Use user home directory + .IPMsgPro
  const home = (typeof window !== 'undefined' && (window as any).__tauricpp__?.homeDir) || '';
  return home ? `${home}/.IPMsgPro`.replace(/\\/g, '/') : '';
};

export default function Settings({ onClose }: SettingsProps) {
  const config = useConfigStore((s) => s.config);
  const saveConfig = useConfigStore((s) => s.saveConfig);
  const [localConfig, setLocalConfig] = useState<Config>({ ...config });
  const [newSegment, setNewSegment] = useState('');

  useEffect(() => {
    setLocalConfig({ ...config });
  }, [config]);

  const handleSave = async () => {
    await saveConfig(localConfig);
    onClose();
  };

  const handleAddSegment = () => {
    if (newSegment.trim() && !localConfig.segments.includes(newSegment.trim())) {
      setLocalConfig({
        ...localConfig,
        segments: [...localConfig.segments, newSegment.trim()],
      });
      setNewSegment('');
    }
  };

  const handleRemoveSegment = (index: number) => {
    setLocalConfig({
      ...localConfig,
      segments: localConfig.segments.filter((_, i) => i !== index),
    });
  };

  const handlePickFolder = async (field: 'dataDir' | 'debugDir') => {
    try {
      const result = await invoke<{ folder?: string }>('dialog.pick_folder', { title: '选择目录' });
      if (result.folder) {
        setLocalConfig({ ...localConfig, [field]: result.folder });
      }
    } catch (e) {
      console.error('Failed to pick folder:', e);
    }
  };

  const handleResetDir = (field: 'dataDir' | 'debugDir') => {
    setLocalConfig({ ...localConfig, [field]: '' });
  };

  const displayDataDir = localConfig.dataDir || getDefaultDataDir() || '~/.IPMsgPro';
  const displayDebugDir = localConfig.debugDir || getDefaultDataDir() || '~/.IPMsgPro';

  return (
    <div className="flex-1 flex flex-col bg-white">
      {/* Header */}
      <div className="h-14 border-b border-gray-200 flex items-center justify-between px-4 shrink-0">
        <h2 className="text-base font-medium text-gray-800">设置</h2>
        <button className="p-1 text-gray-400 hover:text-gray-600" onClick={onClose}>
          <FiX size={20} />
        </button>
      </div>

      {/* Settings content */}
      <div className="flex-1 overflow-y-auto p-6">
        <div className="max-w-lg space-y-6">
          {/* User info */}
          <Section title="用户信息">
            <Field label="昵称">
              <input
                type="text"
                value={localConfig.nickname}
                onChange={(e) => setLocalConfig({ ...localConfig, nickname: e.target.value })}
                className="input-field"
                placeholder="输入昵称"
              />
            </Field>
          </Section>

          {/* Network */}
          <Section title="网络设置">
            <Field label="端口号">
              <input
                type="number"
                value={localConfig.port}
                onChange={(e) => setLocalConfig({ ...localConfig, port: parseInt(e.target.value) || 2425 })}
                className="input-field"
                min={1}
                max={65535}
              />
            </Field>

            <Field label="自动发现用户">
              <label className="flex items-center gap-2 cursor-pointer">
                <input
                  type="checkbox"
                  checked={localConfig.autoDiscovery}
                  onChange={(e) => setLocalConfig({ ...localConfig, autoDiscovery: e.target.checked })}
                  className="w-4 h-4 text-primary-500 rounded"
                />
                <span className="text-sm text-gray-600">启动时自动搜索局域网用户</span>
              </label>
            </Field>
          </Section>

          {/* Segments */}
          <Section title="网段配置">
            <div className="space-y-2">
              {localConfig.segments.map((seg, i) => (
                <div key={i} className="flex items-center gap-2">
                  <span className="flex-1 text-sm bg-gray-50 px-3 py-1.5 rounded border border-gray-200">
                    {seg}
                  </span>
                  <button
                    className="p-1 text-red-400 hover:text-red-600"
                    onClick={() => handleRemoveSegment(i)}
                  >
                    <FiTrash2 size={14} />
                  </button>
                </div>
              ))}
              <div className="flex items-center gap-2">
                <input
                  type="text"
                  value={newSegment}
                  onChange={(e) => setNewSegment(e.target.value)}
                  className="input-field flex-1"
                  placeholder="输入广播地址，如 192.168.1.255"
                  onKeyDown={(e) => e.key === 'Enter' && handleAddSegment()}
                />
                <button
                  className="p-1.5 text-primary-500 hover:text-primary-600"
                  onClick={handleAddSegment}
                >
                  <FiPlus size={16} />
                </button>
              </div>
            </div>
          </Section>

          {/* Data directory */}
          <Section title="数据目录">
            <Field label="聊天记录存储目录">
              <div className="flex items-center gap-2">
                <input
                  type="text"
                  value={localConfig.dataDir}
                  onChange={(e) => setLocalConfig({ ...localConfig, dataDir: e.target.value })}
                  className="input-field flex-1"
                  placeholder={`默认: ${getDefaultDataDir() || '~/.IPMsgPro'}`}
                />
                <button
                  className="p-1.5 text-gray-400 hover:text-primary-500 border border-gray-200 rounded hover:border-primary-300"
                  onClick={() => handlePickFolder('dataDir')}
                  title="浏览选择目录"
                >
                  <FiFolder size={16} />
                </button>
                <button
                  className="p-1.5 text-gray-400 hover:text-primary-500 border border-gray-200 rounded hover:border-primary-300"
                  onClick={() => handleResetDir('dataDir')}
                  title="恢复默认"
                >
                  <FiRotateCcw size={16} />
                </button>
              </div>
              {localConfig.dataDir ? (
                <p className="text-xs text-gray-400 mt-1">当前: {localConfig.dataDir}</p>
              ) : (
                <p className="text-xs text-gray-400 mt-1">默认: {displayDataDir}</p>
              )}
            </Field>

            <Field label="Debug 信息目录">
              <div className="flex items-center gap-2">
                <input
                  type="text"
                  value={localConfig.debugDir}
                  onChange={(e) => setLocalConfig({ ...localConfig, debugDir: e.target.value })}
                  className="input-field flex-1"
                  placeholder={`默认: ${getDefaultDataDir() || '~/.IPMsgPro'}`}
                />
                <button
                  className="p-1.5 text-gray-400 hover:text-primary-500 border border-gray-200 rounded hover:border-primary-300"
                  onClick={() => handlePickFolder('debugDir')}
                  title="浏览选择目录"
                >
                  <FiFolder size={16} />
                </button>
                <button
                  className="p-1.5 text-gray-400 hover:text-primary-500 border border-gray-200 rounded hover:border-primary-300"
                  onClick={() => handleResetDir('debugDir')}
                  title="恢复默认"
                >
                  <FiRotateCcw size={16} />
                </button>
              </div>
              {localConfig.debugDir ? (
                <p className="text-xs text-gray-400 mt-1">当前: {localConfig.debugDir}</p>
              ) : (
                <p className="text-xs text-gray-400 mt-1">默认: {displayDebugDir}</p>
              )}
            </Field>
          </Section>
        </div>
      </div>

      {/* Footer */}
      <div className="border-t border-gray-200 p-4 flex justify-end gap-3 shrink-0">
        <button
          className="px-4 py-2 text-sm text-gray-600 bg-gray-100 rounded hover:bg-gray-200 transition-colors"
          onClick={onClose}
        >
          取消
        </button>
        <button
          className="px-4 py-2 text-sm text-white bg-primary-500 rounded hover:bg-primary-600 transition-colors"
          onClick={handleSave}
        >
          保存
        </button>
      </div>
    </div>
  );
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div>
      <h3 className="text-sm font-medium text-gray-700 mb-3">{title}</h3>
      <div className="space-y-3">{children}</div>
    </div>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div>
      <label className="block text-xs text-gray-500 mb-1">{label}</label>
      {children}
    </div>
  );
}
