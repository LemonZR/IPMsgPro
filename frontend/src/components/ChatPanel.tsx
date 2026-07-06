import React, { useRef, useEffect, useState } from 'react';
import { FiImage, FiFile, FiSmile, FiX, FiCheck, FiAlertCircle, FiDownload, FiFolder } from 'react-icons/fi';
import { useUserStore } from '../stores/userStore';
import { useMessageStore, PendingFileReceive } from '../stores/messageStore';
import { Message } from '../types';
import { invoke } from '../services/bridge';

// ============================================================================
// Send Preview Modal - shown before sending image/file
// ============================================================================

interface SendPreviewProps {
  mode: 'image' | 'file';
  file: File;
  dataUrl?: string;
  onConfirm: () => void;
  onCancel: () => void;
}

function SendPreview({ mode, file, dataUrl, onConfirm, onCancel }: SendPreviewProps) {
  return (
    <div className="fixed inset-0 bg-black/40 z-50 flex items-center justify-center"
      onClick={onCancel}>
      <div className="bg-white rounded-lg shadow-xl w-[400px] max-h-[80vh] flex flex-col"
        onClick={e => e.stopPropagation()}>
        {/* Header */}
        <div className="flex items-center justify-between px-4 py-3 border-b">
          <h3 className="text-sm font-medium text-gray-800">
            {mode === 'image' ? '发送图片' : '发送文件'}
          </h3>
          <button className="p-1 text-gray-400 hover:text-gray-600" onClick={onCancel}>
            <FiX size={16} />
          </button>
        </div>

        {/* Preview */}
        <div className="px-4 py-3 flex-1 overflow-auto">
          {mode === 'image' && dataUrl ? (
            <div className="flex justify-center">
              <img
                src={dataUrl}
                alt="preview"
                className="max-w-full max-h-[300px] rounded object-contain"
              />
            </div>
          ) : (
            <div className="flex items-center gap-3 p-3 bg-gray-50 rounded-lg">
              <FiFile size={28} className="text-gray-400 shrink-0" />
              <div className="min-w-0 flex-1">
                <p className="text-sm font-medium text-gray-800 truncate">{file.name}</p>
                <p className="text-xs text-gray-400">{formatFileSize(file.size)}</p>
              </div>
            </div>
          )}
          <p className="text-xs text-gray-400 mt-2">
            {mode === 'image' ? '图片将通过 TCP 传输发送给对方' : '文件将通过 TCP 传输发送给对方'}
          </p>
        </div>

        {/* Actions */}
        <div className="flex justify-end gap-2 px-4 py-3 border-t">
          <button
            className="px-4 py-1.5 text-sm text-gray-600 bg-gray-100 rounded hover:bg-gray-200 transition-colors"
            onClick={onCancel}
          >
            取消
          </button>
          <button
            className="px-4 py-1.5 text-sm text-white bg-primary-500 rounded hover:bg-primary-600 transition-colors"
            onClick={onConfirm}
          >
            发送
          </button>
        </div>
      </div>
    </div>
  );
}

// ============================================================================
// ChatPanel - main chat component
// ============================================================================

export default function ChatPanel() {
  const currentUser = useUserStore((s) => s.currentUser);
  const sendMessage = useMessageStore((s) => s.sendMessage);
  const sendImage = useMessageStore((s) => s.sendImage);
  const sendFile = useMessageStore((s) => s.sendFile);
  const loadHistory = useMessageStore((s) => s.loadHistory);
  const pendingFileReceives = useMessageStore((s) => s.pendingFileReceives);
  const acceptFileReceive = useMessageStore((s) => s.acceptFileReceive);
  const rejectFileReceive = useMessageStore((s) => s.rejectFileReceive);

  const [inputText, setInputText] = useState('');
  const messageListRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const imageInputRef = useRef<HTMLInputElement>(null);

  // Send preview state
  const [previewFile, setPreviewFile] = useState<File | null>(null);
  const [previewMode, setPreviewMode] = useState<'image' | 'file' | null>(null);
  const [previewDataUrl, setPreviewDataUrl] = useState<string | undefined>();

  const userId = currentUser?.id || '';

  // Get messages for current user
  const userMessages = useMessageStore((s) => s.messages.get(userId)) || [];
  console.log(`[ChatPanel] userId=${userId}, messages count=${userMessages.length}, messageVersion=${useMessageStore.getState().messageVersion}`);

  // Get pending receives for current user
  const currentUserPendingReceives = pendingFileReceives.filter(
    r => r.fromUser === userId
  );

  // Load history when user changes
  useEffect(() => {
    if (userId) {
      loadHistory(userId);
    }
  }, [userId]);

  // Auto-scroll to bottom
  useEffect(() => {
    if (messageListRef.current) {
      messageListRef.current.scrollTop = messageListRef.current.scrollHeight;
    }
  }, [userMessages, currentUserPendingReceives]);

  // ---- Text send ----
  const handleSend = async () => {
    if (!inputText.trim() || !currentUser) return;
    const success = await sendMessage(currentUser.id, inputText.trim());
    if (success) {
      setInputText('');
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.ctrlKey && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  // ---- Image select & preview ----
  const handleImageClick = () => {
    imageInputRef.current?.click();
  };

  const handleImageSelected = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    if (!file.type.startsWith('image/')) {
      alert('请选择图片文件');
      return;
    }

    // Read data URL for preview
    const reader = new FileReader();
    reader.onload = () => {
      setPreviewFile(file);
      setPreviewMode('image');
      setPreviewDataUrl(reader.result as string);
    };
    reader.readAsDataURL(file);
    e.target.value = '';
  };

  const handleImageSend = async () => {
    if (!previewFile || !currentUser || !previewDataUrl) return;

    const base64 = previewDataUrl.split(',')[1];
    await sendImage(currentUser.id, base64, previewFile.name);

    // Close preview
    setPreviewFile(null);
    setPreviewMode(null);
    setPreviewDataUrl(undefined);
  };

  // ---- File select & preview ----
  const handleFileClick = () => {
    fileInputRef.current?.click();
  };

  const handleFileSelected = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    setPreviewFile(file);
    setPreviewMode('file');
    setPreviewDataUrl(undefined);
    e.target.value = '';
  };

  const handleFileSend = async () => {
    if (!previewFile || !currentUser) return;

    const reader = new FileReader();
    reader.onload = async () => {
      const base64 = (reader.result as string).split(',')[1];
      await sendFile(currentUser.id, base64, previewFile.name);

      setPreviewFile(null);
      setPreviewMode(null);
      setPreviewDataUrl(undefined);
    };
    reader.readAsDataURL(previewFile);
  };

  const handlePreviewCancel = () => {
    setPreviewFile(null);
    setPreviewMode(null);
    setPreviewDataUrl(undefined);
  };

  // ---- File receive handlers ----
  const handleAcceptFile = (requestId: string) => {
    // Pass empty savePath, backend will auto-generate using Downloads folder
    acceptFileReceive(requestId, '');
  };

  const handleRejectFile = (requestId: string) => {
    rejectFileReceive(requestId);
  };

  if (!currentUser) return null;

  const statusText = currentUser.status === 'online' ? '在线'
    : currentUser.status === 'away' ? '离开'
    : '离线';

  return (
    <div className="flex-1 flex flex-col bg-white">
      {/* Chat header */}
      <div className="h-14 border-b border-gray-200 flex items-center px-4 shrink-0">
        <div>
          <h3 className="text-sm font-medium text-gray-800">{currentUser.nickname}</h3>
          <p className="text-xs text-gray-400">
            {statusText} · {currentUser.ip}:{currentUser.port}
          </p>
        </div>
      </div>

      {/* Message list */}
      <div ref={messageListRef} className="flex-1 overflow-y-auto p-4 space-y-3 bg-[#F5F5F5]">
        {userMessages.length === 0 && currentUserPendingReceives.length === 0 ? (
          <div className="text-center text-gray-400 text-sm mt-10">
            暂无消息，发送一条消息开始聊天
          </div>
        ) : (
          userMessages.map((msg) => (
            <MessageBubble
              key={msg.id}
              message={msg}
              pendingReceives={pendingFileReceives}
              onAccept={handleAcceptFile}
              onReject={handleRejectFile}
            />
          ))
        )}
      </div>

      {/* Input area */}
      <div className="border-t border-gray-200 shrink-0">
        {/* Toolbar */}
        <div className="flex items-center gap-2 px-4 pt-2">
          <button className="p-1.5 text-gray-400 hover:text-gray-600 rounded transition-colors" title="表情">
            <FiSmile size={18} />
          </button>
          <button
            className="p-1.5 text-gray-400 hover:text-gray-600 rounded transition-colors"
            title="图片"
            onClick={handleImageClick}
          >
            <FiImage size={18} />
          </button>
          <button
            className="p-1.5 text-gray-400 hover:text-gray-600 rounded transition-colors"
            title="文件"
            onClick={handleFileClick}
          >
            <FiFile size={18} />
          </button>
          {/* Hidden file inputs */}
          <input
            ref={imageInputRef}
            type="file"
            accept="image/*"
            className="hidden"
            onChange={handleImageSelected}
          />
          <input
            ref={fileInputRef}
            type="file"
            className="hidden"
            onChange={handleFileSelected}
          />
        </div>

        {/* Text input */}
        <div className="px-4 pb-3 pt-1">
          <textarea
            value={inputText}
            onChange={(e) => setInputText(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="输入消息，Enter发送，Ctrl+Enter换行"
            rows={3}
            className="w-full resize-none text-sm leading-relaxed p-2 rounded border border-gray-200
                       focus:outline-none focus:ring-1 focus:ring-primary-400
                       placeholder-gray-400"
          />
          <div className="flex justify-end mt-1">
            <button
              className="px-4 py-1.5 text-sm text-white bg-primary-500 rounded
                         hover:bg-primary-600 transition-colors disabled:opacity-50
                         disabled:cursor-not-allowed"
              onClick={handleSend}
              disabled={!inputText.trim()}
            >
              发送
            </button>
          </div>
        </div>
      </div>

      {/* Send preview modal */}
      {previewMode && previewFile && (
        <SendPreview
          mode={previewMode}
          file={previewFile}
          dataUrl={previewDataUrl}
          onConfirm={previewMode === 'image' ? handleImageSend : handleFileSend}
          onCancel={handlePreviewCancel}
        />
      )}
    </div>
  );
}

// ============================================================================
// MessageBubble - render different types of messages
// ============================================================================

interface MessageBubbleProps {
  message: Message;
  pendingReceives: PendingFileReceive[];
  onAccept: (requestId: string) => void;
  onReject: (requestId: string) => void;
}

function MessageBubble({ message, pendingReceives, onAccept, onReject }: MessageBubbleProps) {
  const isSelf = message.from === 'self';

  return (
    <div className={`flex ${isSelf ? 'justify-end' : 'justify-start'} message-in`}>
      <div className={`max-w-[70%] ${isSelf ? 'order-2' : ''}`}>
        <div
          className={`px-3 py-2 rounded-lg text-sm leading-relaxed break-words
            ${isSelf
              ? 'bg-chat-sent text-gray-800 rounded-tr-none'
              : 'bg-chat-received text-gray-800 rounded-tl-none shadow-sm'
            }`}
        >
          {renderContent(message, pendingReceives, onAccept, onReject)}
        </div>
        <div className={`text-[10px] text-gray-400 mt-0.5 ${isSelf ? 'text-right' : 'text-left'}`}>
          {formatTime(message.timestamp)}
        </div>
      </div>
    </div>
  );
}

function renderContent(
  message: Message,
  pendingReceives: PendingFileReceive[],
  onAccept: (requestId: string) => void,
  onReject: (requestId: string) => void
) {
  switch (message.type) {
    case 'image':
      return <ImageContent message={message} pendingReceives={pendingReceives} onAccept={onAccept} onReject={onReject} />;
    case 'file':
      return <FileContent message={message} pendingReceives={pendingReceives} onAccept={onAccept} onReject={onReject} />;
    default:
      return <span>{message.content}</span>;
  }
}

// ---- Image message with thumbnail + progress ----
function ImageContent({ message, pendingReceives, onAccept, onReject }: {
  message: Message;
  pendingReceives: PendingFileReceive[];
  onAccept: (requestId: string) => void;
  onReject: (requestId: string) => void;
}) {
  const progress = message.transferProgress;
  const isWaiting = progress === -1;  // waiting for acceptance
  const isTransferring = progress !== undefined && progress !== -1 && progress >= 0 && progress < 100;
  const isCompleted = progress === 100 || (message.status === 'delivered' && message.fileInfo?.filePath);
  const isFailed = message.status === 'failed';

  // Find the pending receive request for this message
  const pendingReq = pendingReceives.find(r => `recv_${r.packetNo}` === message.id);

  return (
    <div className="relative">
      {/* Image display */}
      {message.content.startsWith('data:') ? (
        <img
          src={message.content}
          alt="图片"
          className="max-w-full rounded cursor-pointer hover:opacity-90 transition-opacity"
          style={{ maxHeight: 200 }}
        />
      ) : (
        <div className="flex items-center gap-2 p-1">
          <FiImage size={20} className="text-primary-400 shrink-0" />
          <span className="text-sm text-gray-700">{message.fileInfo?.fileName || message.content}</span>
        </div>
      )}

      {/* File receive confirmation - embedded in message */}
      {isWaiting && pendingReq && (
        <div className="mt-2 p-2 bg-amber-50 rounded border border-amber-200">
          <div className="flex items-center gap-2 mb-2">
            <FiDownload size={14} className="text-amber-600" />
            <span className="text-xs text-amber-700 font-medium">
              对方发送了一张图片，是否接收？
            </span>
          </div>
          <div className="flex items-center gap-2 text-xs text-gray-500 mb-2">
            <span>{message.fileInfo?.fileName}</span>
            {message.fileInfo?.fileSize ? <span>({formatFileSize(message.fileInfo.fileSize)})</span> : null}
          </div>
          <div className="flex gap-2">
            <button
              className="px-3 py-1 text-xs text-white bg-primary-500 rounded hover:bg-primary-600 transition-colors"
              onClick={() => onAccept(pendingReq.id)}
            >
              接收
            </button>
            <button
              className="px-3 py-1 text-xs text-gray-600 bg-gray-100 rounded hover:bg-gray-200 transition-colors"
              onClick={() => onReject(pendingReq.id)}
            >
              拒绝
            </button>
          </div>
        </div>
      )}

      {/* Transfer progress */}
      {isTransferring && (
        <div className="mt-1">
          <div className="w-full h-1 bg-gray-200 rounded-full overflow-hidden">
            <div
              className="h-full bg-primary-500 rounded-full transition-all duration-300"
              style={{ width: `${progress}%` }}
            />
          </div>
          <span className="text-xs text-gray-400">{progress}%</span>
        </div>
      )}

      {/* Transfer completed - show "open folder" link */}
      {isCompleted && message.fileInfo?.filePath && !isSelf(message) && (
        <div className="mt-1 flex items-center gap-1">
          <FiCheck size={12} className="text-green-500" />
          <span className="text-xs text-green-600">已保存</span>
          <button
            className="text-xs text-primary-500 hover:text-primary-600 flex items-center gap-0.5"
            onClick={() => openFolder(message.fileInfo!.filePath!)}
          >
            <FiFolder size={10} />
            打开文件夹
          </button>
        </div>
      )}

      {/* Sent successfully indicator for self-sent images */}
      {isCompleted && isSelf(message) && (
        <div className="mt-1 flex items-center gap-1">
          <FiCheck size={12} className="text-green-500" />
          <span className="text-xs text-green-600">发送成功</span>
        </div>
      )}

      {isFailed && (
        <div className="mt-1 flex items-center gap-1">
          <FiAlertCircle size={12} className="text-red-500" />
          <span className="text-xs text-red-500">传输失败</span>
        </div>
      )}
    </div>
  );
}

// ---- File message with card + progress ----
function FileContent({ message, pendingReceives, onAccept, onReject }: {
  message: Message;
  pendingReceives: PendingFileReceive[];
  onAccept: (requestId: string) => void;
  onReject: (requestId: string) => void;
}) {
  const progress = message.transferProgress;
  const isWaiting = progress === -1;  // waiting for acceptance
  const isTransferring = progress !== undefined && progress !== -1 && progress >= 0 && progress < 100;
  const isCompleted = progress === 100 || (message.status === 'delivered' && message.fileInfo?.filePath);
  const isFailed = message.status === 'failed';
  const isSentByMe = message.from === 'self';

  // Find the pending receive request for this message
  const pendingReq = pendingReceives.find(r => `recv_${r.packetNo}` === message.id);

  return (
    <div className="min-w-[200px]">
      <div className="flex items-center gap-3">
        <div className={`p-2 rounded ${isCompleted ? 'bg-green-50' : 'bg-gray-50'}`}>
          <FiFile size={20} className={isCompleted ? 'text-green-500' : 'text-gray-400'} />
        </div>
        <div className="min-w-0 flex-1">
          <p className="text-sm text-gray-800 truncate font-medium">
            {message.fileInfo?.fileName || message.content}
          </p>
          <p className="text-xs text-gray-400">
            {message.fileInfo?.fileSize ? formatFileSize(message.fileInfo.fileSize) : ''}
          </p>
        </div>
        {isCompleted && isSentByMe && (
          <FiCheck size={14} className="text-green-500 shrink-0" />
        )}
      </div>

      {/* File receive confirmation - embedded in message */}
      {isWaiting && pendingReq && (
        <div className="mt-2 p-2 bg-amber-50 rounded border border-amber-200">
          <div className="flex items-center gap-2 mb-2">
            <FiDownload size={14} className="text-amber-600" />
            <span className="text-xs text-amber-700 font-medium">
              对方发送了一个文件，是否接收？
            </span>
          </div>
          <div className="flex items-center gap-2 text-xs text-gray-500 mb-2">
            <span>{message.fileInfo?.fileName}</span>
            {message.fileInfo?.fileSize ? <span>({formatFileSize(message.fileInfo.fileSize)})</span> : null}
          </div>
          <div className="flex gap-2">
            <button
              className="px-3 py-1 text-xs text-white bg-primary-500 rounded hover:bg-primary-600 transition-colors"
              onClick={() => onAccept(pendingReq.id)}
            >
              接收
            </button>
            <button
              className="px-3 py-1 text-xs text-gray-600 bg-gray-100 rounded hover:bg-gray-200 transition-colors"
              onClick={() => onReject(pendingReq.id)}
            >
              拒绝
            </button>
          </div>
        </div>
      )}

      {/* Progress bar for transferring */}
      {isTransferring && (
        <div className="mt-2">
          <div className="w-full h-1.5 bg-gray-200 rounded-full overflow-hidden">
            <div
              className="h-full bg-primary-500 rounded-full transition-all duration-300"
              style={{ width: `${progress}%` }}
            />
          </div>
          <span className="text-xs text-gray-400">{progress}%</span>
        </div>
      )}

      {/* Transfer completed - show save path and "open folder" link */}
      {isCompleted && !isSentByMe && message.fileInfo?.filePath && (
        <div className="mt-2 space-y-1">
          <div className="flex items-center gap-1">
            <FiCheck size={12} className="text-green-500" />
            <span className="text-xs text-green-600">接收成功</span>
          </div>
          <div className="text-xs text-gray-400 truncate" title={message.fileInfo.filePath}>
            保存至：{message.fileInfo.filePath}
          </div>
          <button
            className="text-xs text-primary-500 hover:text-primary-600 flex items-center gap-0.5"
            onClick={() => openFolder(message.fileInfo!.filePath!)}
          >
            <FiFolder size={10} />
            打开文件夹
          </button>
        </div>
      )}

      {/* Sent successfully for self-sent files */}
      {isCompleted && isSentByMe && (
        <div className="mt-1 flex items-center gap-1">
          <FiCheck size={12} className="text-green-500" />
          <span className="text-xs text-green-600">发送成功</span>
        </div>
      )}

      {isFailed && (
        <div className="mt-1 flex items-center gap-1">
          <FiAlertCircle size={12} className="text-red-500" />
          <span className="text-xs text-red-500">传输失败</span>
        </div>
      )}
    </div>
  );
}

// Helper to check if message is from self (needed inside component functions)
function isSelf(message: Message): boolean {
  return message.from === 'self';
}

// ============================================================================
// Utility functions
// ============================================================================

function formatTime(timestamp: number): string {
  const date = new Date(timestamp);
  const now = new Date();
  const isToday = date.toDateString() === now.toDateString();

  if (isToday) {
    return date.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
  }
  return date.toLocaleDateString('zh-CN', { month: '2-digit', day: '2-digit' }) + ' ' +
    date.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
}

function formatFileSize(bytes: number): string {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

/** Open the containing folder of a file */
function openFolder(filePath: string) {
  // Use shell command to open the folder and select the file
  // In Tauri, we'd need to invoke a backend command
  // For now, just copy path to clipboard and alert
  try {
    // Try to use the backend to open folder
    invoke('file.open_folder', { path: filePath }).catch(() => {
      // Fallback: copy to clipboard
      navigator.clipboard.writeText(filePath).then(() => {
        alert(`文件路径已复制到剪贴板：${filePath}`);
      }).catch(() => {
        alert(`文件保存位置：${filePath}`);
      });
    });
  } catch {
    alert(`文件保存位置：${filePath}`);
  }
}
