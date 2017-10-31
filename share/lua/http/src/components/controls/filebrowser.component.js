import { notifyBus } from '../../services/bus.service.js';
import { VIDEO_TYPES, AUDIO_TYPES, PLAYLIST_TYPES } from '../../services/initialize.service.js';

Vue.component('file-modal', {
    template: '#file-modal-template',
    methods: {
        populateTree() {
            $('#file-tree').jstree({
                core: {
                    multiple: false,
                    animation: 100,
                    check_callback: true,
                    html_titles: true,
                    load_open: true,
                    themes: {
                        variant: 'medium',
                        dots: false
                    },
                    data: {
                        url: (node) => {
                            if (node.id === '#') {
                                return 'requests/browse.json?dir=/';
                            }
                            return `requests/browse.json?dir=/${node.data.path}`;
                        },
                        dataType: 'json',
                        dataFilter(rawData) {
                            const data = JSON.parse(rawData);
                            const result = data.element.map((d) => {
                                const res = {
                                    text: d.name,
                                    data: {
                                        path: d.path,
                                        uri: d.uri,
                                        type: d.type
                                    },
                                    type: d.type,
                                    children: true
                                };
                                if (d.type === 'file') {
                                    res.children = false;
                                }
                                return res;
                            });
                            return JSON.stringify(result);
                        }
                    }
                },
                types: {
                    '#': {
                        valid_children: ['root']
                    },
                    root: {
                        valid_children: ['default']
                    },
                    default: {
                        valid_children: ['default', 'file']
                    }
                },
                plugins: [
                    'themes',
                    'json_data',
                    'ui',
                    'cookies',
                    'crrm',
                    'sort',
                    'types'
                ]
            });

            $('#file-tree').on('select_node.jstree', (e, data) => {
                node = data.instance.get_node(data.selected[0]);
                ext = (node.data.uri).substr(node.data.uri.lastIndexOf('.') + 1).toLowerCase();
                if (node.data.type === 'file' && ($.inArray(ext, VIDEO_TYPES) !== -1 || $.inArray(ext, AUDIO_TYPES) !== -1 || $.inArray(ext, PLAYLIST_TYPES) !== -1)) {
                    notifyBus('addItem', [1, '', node.data.uri,node.data.uri]);
                }
            }).jstree();
        }
    },
    mounted() {
        this.populateTree();
    }
});
