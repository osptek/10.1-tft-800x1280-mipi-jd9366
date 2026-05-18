class ModernUpload {
    constructor() {
        this.files = [];
        this.maxImageSize = { width: 1920, height: 1080 };
        this.allowedTypes = {
            image: ['image/jpeg', 'image/jpg', 'image/png', 'image/gif', 'image/bmp'],
            video: ['video/mp4', 'video/avi', 'video/mov']
        };
        this.init();
    }

    init() {
        this.setupEventListeners();
        this.loadFileList();
        this.updateQualityDisplay();
    }

    setupEventListeners() {
        // File input and upload zone
        const uploadZone = document.getElementById('uploadZone');
        const fileInput = document.getElementById('fileInput');
        const uploadBtn = document.getElementById('uploadBtn');
        const clearBtn = document.getElementById('clearBtn');
        const refreshBtn = document.getElementById('refreshBtn');
        const qualitySlider = document.getElementById('quality');

        // Upload zone events
        uploadZone.addEventListener('click', () => fileInput.click());
        uploadZone.addEventListener('dragover', this.handleDragOver.bind(this));
        uploadZone.addEventListener('dragleave', this.handleDragLeave.bind(this));
        uploadZone.addEventListener('drop', this.handleDrop.bind(this));

        // File input change
        fileInput.addEventListener('change', (e) => this.handleFileSelect(e.target.files));

        // Button events
        uploadBtn.addEventListener('click', this.uploadFiles.bind(this));
        clearBtn.addEventListener('click', this.clearFiles.bind(this));
        refreshBtn.addEventListener('click', () => {
            this.loadFileList();
        });

        // Quality slider
        qualitySlider.addEventListener('input', this.updateQualityDisplay.bind(this));

        // Prevent default drag behaviors
        ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
            document.addEventListener(eventName, this.preventDefaults, false);
        });
    }

    preventDefaults(e) {
        e.preventDefault();
        e.stopPropagation();
    }

    handleDragOver(e) {
        e.preventDefault();
        document.getElementById('uploadZone').classList.add('dragover');
    }

    handleDragLeave(e) {
        e.preventDefault();
        document.getElementById('uploadZone').classList.remove('dragover');
    }

    handleDrop(e) {
        e.preventDefault();
        document.getElementById('uploadZone').classList.remove('dragover');
        const files = e.dataTransfer.files;
        this.handleFileSelect(files);
    }

    handleFileSelect(files) {
        Array.from(files).forEach(file => {
            if (this.validateFile(file)) {
                this.files.push(file);
            }
        });
        this.updateFileList();
        this.updateButtons();
    }

    validateFile(file) {
        const allAllowedTypes = [...this.allowedTypes.image, ...this.allowedTypes.video];

        if (!allAllowedTypes.includes(file.type)) {
            this.showToast(`File type not supported: ${file.name}`, 'error');
            return false;
        }

        if (file.size > 100 * 1024 * 1024) { // 100MB limit
            this.showToast(`File too large: ${file.name} (max 100MB)`, 'error');
            return false;
        }

        return true;
    }

    async updateFileList() {
        const fileList = document.getElementById('fileList');
        fileList.innerHTML = '';

        for (const file of this.files) {
            const fileItem = document.createElement('div');
            fileItem.className = 'file-item';

            const preview = await this.createFilePreview(file);
            const fileInfo = this.createFileInfo(file);
            const fileActions = this.createFileActions(file);

            fileItem.appendChild(preview);
            fileItem.appendChild(fileInfo);
            fileItem.appendChild(fileActions);

            fileList.appendChild(fileItem);
        }
    }

    async createFilePreview(file) {
        const preview = document.createElement('div');
        preview.className = 'file-preview';

        if (file.type.startsWith('image/')) {
            preview.textContent = '🖼️';
        } else if (file.type.startsWith('video/')) {
            preview.textContent = '🎬';
        } else {
            preview.textContent = '📄';
        }

        return preview;
    }

    createFileInfo(file) {
        const fileInfo = document.createElement('div');
        fileInfo.className = 'file-info-item';

        const fileName = document.createElement('div');
        fileName.className = 'file-name';

        if (file.originalName && file.originalName !== file.name) {
            fileName.innerHTML = `
                <div style="font-weight: 600;">${file.name}</div>
                <div style="font-size: 0.8rem; color: #6c757d; font-style: italic;">Original Name: ${file.originalName}</div>
            `;
        } else {
            fileName.textContent = file.name;
        }

        const fileDetails = document.createElement('div');
        fileDetails.className = 'file-details';
        fileDetails.textContent = `${this.formatFileSize(file.size)} • ${file.type}`;

        fileInfo.appendChild(fileName);
        fileInfo.appendChild(fileDetails);

        return fileInfo;
    }

    createFileActions(file) {
        const actions = document.createElement('div');
        actions.className = 'file-actions';

        const removeBtn = document.createElement('button');
        removeBtn.className = 'btn btn-danger btn-small';
        removeBtn.innerHTML = '🗑️ Remove';
        removeBtn.onclick = () => this.removeFile(file);

        actions.appendChild(removeBtn);
        return actions;
    }

    removeFile(file) {
        const index = this.files.indexOf(file);
        if (index > -1) {
            this.files.splice(index, 1);
            this.updateFileList();
            this.updateButtons();
        }
    }

    clearFiles() {
        this.files = [];
        this.updateFileList();
        this.updateButtons();
        this.showToast('All files cleared', 'info');
    }

    updateButtons() {
        const uploadBtn = document.getElementById('uploadBtn');
        const clearBtn = document.getElementById('clearBtn');

        uploadBtn.disabled = this.files.length === 0;
        clearBtn.disabled = this.files.length === 0;
    }

    async uploadFiles() {
        if (this.files.length === 0) return;

        const progressSection = document.getElementById('progressSection');
        const progressFill = document.getElementById('progressFill');
        const progressText = document.getElementById('progressText');
        const progressDetails = document.getElementById('progressDetails');

        progressSection.style.display = 'block';

        try {
            for (let i = 0; i < this.files.length; i++) {
                const file = this.files[i];
                const processedFile = await this.processFile(file);

                progressDetails.textContent = `Uploading ${file.name}...`;

                await this.uploadSingleFile(processedFile);

                const progress = ((i + 1) / this.files.length) * 100;
                progressFill.style.width = `${progress}%`;
                progressText.textContent = `${Math.round(progress)}%`;
            }

            this.showToast('All files uploaded successfully!', 'success');
            this.clearFiles();
            this.loadFileList();

        } catch (error) {
            this.showToast(`Upload failed: ${error.message}`, 'error');
        } finally {
            setTimeout(() => {
                progressSection.style.display = 'none';
            }, 2000);
        }
    }

    async processFile(file) {
        // Process all images to align dimensions to multiples of 8
        if (file.type.startsWith('image/')) {
            return await this.compressImage(file);
        }

        const newFilename = this.generateNewFilename(file);
        const renamedFile = this.createRenamedFile(file, newFilename);

        console.log(`Renaming non-image file: ${file.name} -> ${newFilename}`);
        return renamedFile;
    }

    async compressImage(file) {
        return new Promise((resolve) => {
            const canvas = document.createElement('canvas');
            const ctx = canvas.getContext('2d');
            const img = new Image();

            img.onload = () => {
                const { width, height } = img;

                // Always align dimensions to multiples of 8 for all images
                let newWidth = Math.round(width / 8) * 8;
                let newHeight = Math.round(height / 8) * 8;

                // Ensure minimum size of 8x8
                newWidth = Math.max(newWidth, 8);
                newHeight = Math.max(newHeight, 8);

                // Check if image needs compression (if larger than max size)
                if (width > this.maxImageSize.width || height > this.maxImageSize.height) {
                    const ratio = Math.min(this.maxImageSize.width / width, this.maxImageSize.height / height);
                    newWidth = Math.round(width * ratio);
                    newHeight = Math.round(height * ratio);

                    // Ensure compressed dimensions are also multiples of 8
                    newWidth = Math.round(newWidth / 8) * 8;
                    newHeight = Math.round(newHeight / 8) * 8;

                    // Ensure minimum size of 8x8
                    newWidth = Math.max(newWidth, 8);
                    newHeight = Math.max(newHeight, 8);
                }

                canvas.width = newWidth;
                canvas.height = newHeight;

                // Draw and compress
                ctx.drawImage(img, 0, 0, newWidth, newHeight);

                const quality = document.getElementById('quality').value / 100;
                canvas.toBlob((blob) => {
                    const newFilename = this.generateNewFilename(file);
                    const compressedFile = new File([blob], newFilename, {
                        type: file.type,
                        lastModified: file.lastModified
                    });
                    compressedFile.originalName = file.name;
                    resolve(compressedFile);
                }, file.type, quality);
            };

            img.src = URL.createObjectURL(file);
        });
    }

    async uploadSingleFile(file) {
        console.log('Uploading file:', file.name, 'Size:', file.size, 'Type:', file.type);

        const formData = new FormData();
        formData.append('file', file);

        try {
            const uploadUrl = '/upload';

            const response = await fetch(uploadUrl, {
                method: 'POST',
                headers: {
                    'Accept': 'application/json'
                },
                body: formData
            });

            console.log('Upload response status:', response.status);
            console.log('Upload URL:', uploadUrl);

            if (!response.ok) {
                const errorText = await response.text();
                console.error('Upload error response:', errorText);

                // Handle specific error cases
                if (response.status === 400 && errorText.includes('File already exists')) {
                    throw new Error(`File "${file.name}" already exists`);
                } else if (response.status === 400 && errorText.includes('Unsupported file format')) {
                    throw new Error(`Unsupported file format for "${file.name}"`);
                } else {
                    throw new Error(`Upload failed (${response.status}): ${errorText}`);
                }
            }

            // Check if response is JSON
            const contentType = response.headers.get('content-type');
            if (contentType && contentType.includes('application/json')) {
                return await response.json();
            } else {
                // If not JSON, just return success
                return { success: true };
            }
        } catch (error) {
            console.error('Upload error:', error);
            throw error;
        }
    }

    async loadFileList() {
        try {
            const response = await fetch('/files');
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }

            const data = await response.json();
            this.displayFileGrid(data.items || []);
        } catch (error) {
            console.error('Failed to load file list:', error);
            this.showToast('Failed to load file list', 'error');
        }
    }

    displayFileGrid(items) {
        const fileGrid = document.getElementById('fileGrid');
        fileGrid.innerHTML = '';

        if (items.length === 0) {
            fileGrid.innerHTML += '<div style="text-align: center; color: #6c757d; font-size: 1.2rem; padding: 40px;">No files found</div>';
            return;
        }

        const sortedItems = items.sort((a, b) => a.name.localeCompare(b.name));

        sortedItems.forEach(item => {
            const itemCard = this.createFileCard(item);
            fileGrid.appendChild(itemCard);
        });
    }

    createFileCard(item) {
        const card = document.createElement('div');
        card.className = 'file-card';

        // File icon (left side)
        const icon = document.createElement('div');
        icon.className = 'file-card-icon';

        if (item.type && item.type.startsWith('image/')) {
            icon.textContent = '🖼️';
        } else if (item.type && item.type.startsWith('video/')) {
            icon.textContent = '🎬';
        } else {
            icon.textContent = '📄';
        }

        // File info (center)
        const info = document.createElement('div');
        info.className = 'file-card-info';

        const name = document.createElement('div');
        name.className = 'file-card-name';
        name.textContent = item.name;

        const details = document.createElement('div');
        details.className = 'file-card-details';
        details.textContent = `${this.formatFileSize(item.size)} • ${item.type || 'Unknown'}`;

        info.appendChild(name);
        info.appendChild(details);

        // Actions (right side)
        const actions = document.createElement('div');
        actions.className = 'file-card-actions';

        const viewBtn = document.createElement('a');
        viewBtn.href = item.path;
        viewBtn.target = '_blank';
        viewBtn.className = 'btn btn-secondary btn-small';
        viewBtn.innerHTML = '👁️ View';

        const deleteBtn = document.createElement('button');
        deleteBtn.className = 'btn btn-danger btn-small';
        deleteBtn.innerHTML = '🗑️ Delete';
        deleteBtn.onclick = (e) => {
            e.stopPropagation();
            this.deleteFile(item.path);
        };

        actions.appendChild(viewBtn);
        actions.appendChild(deleteBtn);

        card.appendChild(icon);
        card.appendChild(info);
        card.appendChild(actions);

        return card;
    }

    async deleteFile(filePath) {
        const filename = filePath.split('/').pop();
        if (!confirm(`Are you sure you want to delete "${filename}"?`)) {
            return;
        }

        try {
            const response = await fetch(`/delete${filePath}`, {
                method: 'DELETE'
            });

            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }

            this.showToast(`File "${filename}" deleted successfully`, 'success');
            this.loadFileList();
        } catch (error) {
            this.showToast(`Failed to delete file: ${error.message}`, 'error');
        }
    }

    updateQualityDisplay() {
        const quality = document.getElementById('quality').value;
        document.getElementById('qualityValue').textContent = `${quality}%`;
    }

    formatFileSize(bytes) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    showToast(message, type = 'info') {
        const toast = document.getElementById('toast');
        toast.textContent = message;
        toast.className = `toast ${type}`;
        toast.classList.add('show');

        setTimeout(() => {
            toast.classList.remove('show');
        }, 3000);
    }

    createRenamedFile(originalFile, newFilename) {

        const renamedFile = new File([originalFile], newFilename, {
            type: originalFile.type,
            lastModified: originalFile.lastModified
        });

        renamedFile.originalName = originalFile.name;

        return renamedFile;
    }

    generateNewFilename(originalFile) {
        const now = new Date();
        const year = now.getFullYear();
        const month = String(now.getMonth() + 1).padStart(2, '0');
        const day = String(now.getDate()).padStart(2, '0');
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');

        const randomNum = String(Math.floor(Math.random() * 10000)).padStart(4, '0');

        const originalName = originalFile.name;
        const lastDotIndex = originalName.lastIndexOf('.');
        let extension = '';
        if (lastDotIndex > 0) {
            extension = originalName.substring(lastDotIndex + 1).toLowerCase();
        } else {
            if (originalFile.type.startsWith('image/')) {
                if (originalFile.type === 'image/jpeg') extension = 'jpg';
                else if (originalFile.type === 'image/png') extension = 'png';
                else if (originalFile.type === 'image/gif') extension = 'gif';
                else if (originalFile.type === 'image/bmp') extension = 'bmp';
                else extension = 'jpg';
            } else if (originalFile.type.startsWith('video/')) {
                if (originalFile.type === 'video/mp4') extension = 'mp4';
                else if (originalFile.type === 'video/avi') extension = 'avi';
                else if (originalFile.type === 'video/quicktime') extension = 'mov';
                else extension = 'mp4';
            } else {
                extension = 'bin';
            }
        }

        const newFilename = `${year}-${month}-${day}_${hours}${minutes}${seconds}_${randomNum}.${extension}`;

        console.log(`Renaming file: ${originalName} -> ${newFilename}`);
        return newFilename;
    }
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    new ModernUpload();
});
