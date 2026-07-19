import React, { useState, useEffect } from 'react';
import { FiX, FiPlus, FiTrash2, FiFolder, FiRotateCcw, FiMonitor, FiMinimize2, FiVolume2, FiVolumeX } from 'react-icons/fi';
import { useConfigStore } from '../stores/configStore';
import { Config, DEFAULT_CONFIG, APP_VERSION } from '../types';
import { invoke } from '../services/bridge';

interface SettingsProps {
  onClose: () => void;
}

/** Default data directory under user home */
const getDefaultDataDir = (): string => {
  // Use backend-injected defaultDataDir (matches backend GetAppDataDir)
  return (typeof window !== 'undefined' && (window as any).__tauricpp__?.defaultDataDir) || '';
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

  const handlePickFolder = async () => {
    try {
      const result = await invoke<{ success?: boolean; folder?: string }>('dialog.pick_folder', { title: '选择聊天记录目录' });
      console.log('[Settings] dialog.pick_folder result:', JSON.stringify(result));
      if (result.success && result.folder) {
        setLocalConfig({ ...localConfig, dataDir: result.folder });
      }
    } catch (e) {
      console.error('Failed to pick folder:', e);
    }
  };

  const handleResetDir = () => {
    setLocalConfig({ ...localConfig, dataDir: '' });
  };

  const displayDataDir = localConfig.dataDir || getDefaultDataDir() || '~/.ipmsgpro';

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
                  placeholder={`默认: ${getDefaultDataDir() || '~/.ipmsgpro'}`}
                />
                <button
                  className="p-1.5 text-gray-400 hover:text-primary-500 border border-gray-200 rounded hover:border-primary-300"
                  onClick={handlePickFolder}
                  title="浏览选择目录"
                >
                  <FiFolder size={16} />
                </button>
                <button
                  className="p-1.5 text-gray-400 hover:text-primary-500 border border-gray-200 rounded hover:border-primary-300"
                  onClick={handleResetDir}
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
          </Section>

          {/* Notification sound */}
          <Section title="消息提示">
            <Field label="新消息提示音">
              <label className="flex items-center gap-2 cursor-pointer">
                <input
                  type="checkbox"
                  checked={localConfig.notificationSound}
                  onChange={(e) => setLocalConfig({ ...localConfig, notificationSound: e.target.checked })}
                  className="w-4 h-4 text-primary-500 rounded"
                />
                <span className="text-sm text-gray-600">
                  {localConfig.notificationSound ? <FiVolume2 size={14} className="inline mr-1" /> : <FiVolumeX size={14} className="inline mr-1" />}
                  收到新消息时播放提示音
                </span>
              </label>
            </Field>
          </Section>

          {/* Window behavior */}
          <Section title="窗口行为">
            <Field label="关闭/最小化行为">
              <div className="flex gap-3">
                <button
                  className={`flex items-center gap-2 px-3 py-1.5 text-sm rounded border transition-colors ${
                    localConfig.minimizeBehavior === 'taskbar'
                      ? 'border-primary-500 bg-primary-50 text-primary-600'
                      : 'border-gray-200 text-gray-500 hover:border-primary-300'
                  }`}
                  onClick={() => setLocalConfig({ ...localConfig, minimizeBehavior: 'taskbar' })}
                >
                  <FiMinimize2 size={14} />
                  最小化到任务栏
                </button>
                <button
                  className={`flex items-center gap-2 px-3 py-1.5 text-sm rounded border transition-colors ${
                    localConfig.minimizeBehavior === 'tray'
                      ? 'border-primary-500 bg-primary-50 text-primary-600'
                      : 'border-gray-200 text-gray-500 hover:border-primary-300'
                  }`}
                  onClick={() => setLocalConfig({ ...localConfig, minimizeBehavior: 'tray' })}
                >
                  <FiMonitor size={14} />
                  最小化到系统托盘
                </button>
              </div>
              <p className="text-xs text-gray-400 mt-1">
                {localConfig.minimizeBehavior === 'tray'
                  ? '关闭窗口时隐藏到系统托盘，右键托盘图标可退出'
                  : '关闭窗口时退出程序'}
              </p>
            </Field>
          </Section>

          {/* Version info */}
          <Section title="关于">
            <div className="text-sm text-gray-500">
              倍信 (IPMsg Pro) v{APP_VERSION}
            </div>
            <p className="text-xs text-gray-400 mt-1">
              兼容飞秋和IPMsg v3.0协议的局域网即时通讯
            </p>
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
